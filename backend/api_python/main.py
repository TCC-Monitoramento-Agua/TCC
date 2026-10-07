# Versão de deploy: 1
# Incrementar este número em cada commit para alterar o arquivo monitorado pelo Render.

import os
import re
import math
from uuid import UUID
from contextlib import closing
from datetime import datetime
from typing import Any
from zoneinfo import ZoneInfo

from dotenv import load_dotenv
from flask import Flask, jsonify, request
from flask_cors import CORS
import mysql.connector

app = Flask(__name__)
CORS(app)
load_dotenv()

# DATETIME no MySQL representa o horário local de Brasília.
BRAZIL_TIMEZONE = ZoneInfo("America/Sao_Paulo")

DB_CONNECT_TIMEOUT = int(os.getenv("DB_CONNECT_TIMEOUT", 10))
DB_SSL_VERIFY = os.getenv("DB_SSL_VERIFY", "true").lower() in ("1", "true", "yes", "on")


def build_db_ssl_config() -> dict[str, Any]:
    ssl_ca = os.getenv("DB_SSL_CA")
    ssl_cert = os.getenv("DB_SSL_CERT")
    ssl_key = os.getenv("DB_SSL_KEY")

    if not ssl_ca and not ssl_cert and not ssl_key:
        return {}

    ssl_config: dict[str, Any] = {}
    if ssl_ca:
        ssl_config["ssl_ca"] = ssl_ca
    if ssl_cert:
        ssl_config["ssl_cert"] = ssl_cert
    if ssl_key:
        ssl_config["ssl_key"] = ssl_key

    ssl_config["ssl_verify_cert"] = DB_SSL_VERIFY
    return ssl_config


def get_connection():
    db_host = os.getenv("DB_HOST")
    db_user = os.getenv("DB_USER")
    db_password = os.getenv("DB_PASSWORD")
    db_name = os.getenv("DB_NAME")
    db_port = os.getenv("DB_PORT")

    missing_vars = []
    if not db_host:
        missing_vars.append("DB_HOST")
    if not db_user:
        missing_vars.append("DB_USER")
    if not db_password:
        missing_vars.append("DB_PASSWORD")
    if not db_name:
        missing_vars.append("DB_NAME")
    if not db_port:
        missing_vars.append("DB_PORT")

    if missing_vars:
        error_msg = f"Variáveis de ambiente obrigatórias faltando: {', '.join(missing_vars)}"
        app.logger.error(error_msg)
        raise ValueError(error_msg)

    try:
        db_port_int = int(db_port)
    except (ValueError, TypeError):
        raise ValueError(f"DB_PORT deve ser um número inteiro, recebido: {db_port}")

    connection_kwargs = {
        "host": db_host,
        "user": db_user,
        "password": db_password,
        "database": db_name,
        "port": db_port_int,
        "connection_timeout": DB_CONNECT_TIMEOUT,
    }
    connection_kwargs.update(build_db_ssl_config())
    return mysql.connector.connect(**connection_kwargs)


def init_db() -> None:
    with closing(get_connection()) as conn:
        cursor = conn.cursor()
        cursor.execute(
            """
            CREATE TABLE IF NOT EXISTS leituras (
                id INT AUTO_INCREMENT PRIMARY KEY,
                equipamento_id VARCHAR(64) NULL,
                leitura_id CHAR(36) NULL,
                ph FLOAT NOT NULL,
                turbidez FLOAT NOT NULL,
                temperatura FLOAT NOT NULL,
                orp FLOAT NULL,
                data_hora DATETIME NOT NULL
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """
        )
        # Compatibilidade com bancos existentes: preserva leituras antigas como NULL.
        cursor.execute("SHOW COLUMNS FROM leituras LIKE 'equipamento_id'")
        if cursor.fetchone() is None:
            try:
                cursor.execute("ALTER TABLE leituras ADD COLUMN equipamento_id VARCHAR(64) NULL AFTER id")
            except mysql.connector.Error as error:
                # Outro worker pode ter aplicado a migração ao mesmo tempo.
                if error.errno != 1060:
                    raise
        cursor.execute("SHOW COLUMNS FROM leituras LIKE 'leitura_id'")
        if cursor.fetchone() is None:
            try:
                cursor.execute("ALTER TABLE leituras ADD COLUMN leitura_id CHAR(36) NULL AFTER equipamento_id")
            except mysql.connector.Error as error:
                if error.errno != 1060:
                    raise
        cursor.execute("SHOW INDEX FROM leituras WHERE Key_name = 'uq_equipamento_leitura'")
        if not cursor.fetchall():
            try:
                cursor.execute("ALTER TABLE leituras ADD UNIQUE KEY uq_equipamento_leitura (equipamento_id, leitura_id)")
            except mysql.connector.Error as error:
                if error.errno != 1061:
                    raise
        conn.commit()
        cursor.close()


def ensure_db_schema() -> None:
    try:
        init_db()
    except mysql.connector.Error as error:
        app.logger.error("Falha ao inicializar o banco de dados: %s", error)
        raise


def validar_dados(dados: dict[str, Any]) -> tuple[bool, str]:
    if not isinstance(dados, dict):
        return False, "O JSON deve ser um objeto"
    equipamento_id = dados.get("equipamento_id")
    if equipamento_id is not None and (
        not isinstance(equipamento_id, str)
        or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,64}", equipamento_id)
    ):
        return False, "equipamento_id deve ter de 1 a 64 caracteres: letras, números, _, ., : ou -"

    if "leitura_id" in dados:
        try:
            if not isinstance(dados["leitura_id"], str):
                raise ValueError()
            UUID(dados["leitura_id"])
        except (ValueError, AttributeError):
            return False, "leitura_id deve ser um UUID válido"
        if not equipamento_id:
            return False, "equipamento_id é obrigatório quando leitura_id é informado"
        if not dados.get("data_hora"):
            return False, "data_hora é obrigatório quando leitura_id é informado"

    campos_obrigatorios = ["ph", "turbidez", "temperatura"]

    for campo in campos_obrigatorios:
        if campo not in dados:
            return False, f"Campo obrigatório ausente: {campo}"

    try:
        float(dados["ph"])
        float(dados["turbidez"])
        float(dados["temperatura"])

        if "orp" in dados and dados["orp"] is not None:
            float(dados["orp"])

        data_hora = dados.get("data_hora")
        if data_hora is not None and data_hora != "":
            if not isinstance(data_hora, str):
                return False, "data_hora deve ser uma string no formato ISO 8601"
            datetime.fromisoformat(data_hora.strip())

    except (TypeError, ValueError):
        return False, "Os campos numéricos devem conter valores válidos ou data_hora está em formato inválido"

    return True, ""


def parse_data_hora(data_hora: Any) -> str:
    if not data_hora:
        return datetime.now(BRAZIL_TIMEZONE).strftime("%Y-%m-%d %H:%M:%S")

    try:
        data_hora_parsed = datetime.fromisoformat(data_hora.strip())
        if data_hora_parsed.tzinfo is not None:
            data_hora_parsed = data_hora_parsed.astimezone(BRAZIL_TIMEZONE)
        return data_hora_parsed.strftime("%Y-%m-%d %H:%M:%S")
    except (AttributeError, ValueError) as error:
        raise ValueError("Formato inválido para data_hora. Use ISO 8601 ou 'YYYY-MM-DD HH:MM:SS'.") from error




@app.route("/", methods=["GET"])
def home():
    return jsonify(
        {
            "status": "ok",
            "message": "API de monitoramento da água em execução"
        }
    ), 200

@app.route("/status", methods=["GET"])
def status():
    try:
        with closing(get_connection()) as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT 1")
            cursor.fetchone()
            cursor.close()

        return jsonify(
            {
                "status": "ok",
                "api": "online",
                "database": "conectado"
            }
        ), 200

    except Exception as erro:
        return jsonify(
            {
                "status": "erro",
                "api": "online",
                "database": "desconectado",
                "detalhe": str(erro)
            }
        ), 500

@app.route("/leituras", methods=["POST"])
def receber_leitura():
    dados = request.get_json(silent=True)

    if not dados:
        return jsonify(
            {
                "status": "erro",
                "message": "JSON inválido ou ausente"
            }
        ), 400

    valido, mensagem = validar_dados(dados)

    if not valido:
        return jsonify(
            {
                "status": "erro",
                "message": mensagem
            }
        ), 400

    try:
        ph = float(dados["ph"])
        turbidez = float(dados["turbidez"])
        temperatura = float(dados["temperatura"])
        orp = float(dados["orp"]) if "orp" in dados and dados["orp"] is not None else None
        data_hora = parse_data_hora(dados.get("data_hora"))
        medicao_id = str(UUID(dados["leitura_id"])) if "leitura_id" in dados else None
        if not all(math.isfinite(value) for value in (ph, turbidez, temperatura) + (() if orp is None else (orp,))):
            raise ValueError("Os valores devem ser números finitos")

    except ValueError as error:
        return jsonify(
            {
                "status": "erro",
                "message": str(error)
            }
        ), 400

    try:
        with closing(get_connection()) as conn:
            cursor = conn.cursor()

            duplicada = False
            try:
                cursor.execute(
                    """
                    INSERT INTO leituras
                    (equipamento_id, leitura_id, ph, turbidez, temperatura, orp, data_hora)
                    VALUES (%s, %s, %s, %s, %s, %s, %s)
                    """,
                    (dados.get("equipamento_id"), medicao_id, ph, turbidez, temperatura, orp, data_hora),
                )
                conn.commit()
                registro_id = cursor.lastrowid
            except mysql.connector.Error as error:
                conn.rollback()
                if error.errno != 1062 or medicao_id is None:
                    raise
                cursor.execute(
                    "SELECT id, ph, turbidez, temperatura, orp, data_hora FROM leituras WHERE equipamento_id = %s AND leitura_id = %s",
                    (dados["equipamento_id"], medicao_id),
                )
                existente = cursor.fetchone()
                if existente is None:
                    raise
                valores_iguais = all(
                    (old is None and new is None) or
                    (old is not None and new is not None and math.isclose(float(old), new, rel_tol=1e-6, abs_tol=1e-6))
                    for old, new in zip(existente[1:5], (ph, turbidez, temperatura, orp))
                )
                if not valores_iguais or existente[5].strftime("%Y-%m-%d %H:%M:%S") != data_hora:
                    cursor.close()
                    return jsonify({"status": "erro", "message": "leitura_id já utilizado com outros dados"}), 409
                registro_id = existente[0]
                duplicada = True
            cursor.close()

        return jsonify(
            {
                "status": "ok",
                "mensagem": "Leitura já registrada" if duplicada else "Leitura salva com sucesso",
                "id": registro_id,
                "equipamento_id": dados.get("equipamento_id"),
                "leitura_id": medicao_id,
                "duplicada": duplicada,
            }
        ), 200 if duplicada else 201

    except mysql.connector.Error as erro:
        return jsonify(
            {
                "status": "erro",
                "message": "Falha ao salvar leitura",
                "detalhe": str(erro)
            }
        ), 500

    except Exception as erro:
        return jsonify(
            {
                "status": "erro",
                "message": "Erro inesperado ao salvar leitura",
                "detalhe": str(erro)
            }
        ), 500


@app.route("/leituras", methods=["GET"])
def listar_leituras():
    limite = request.args.get("limite", default=10, type=int)
    if not 1 <= limite <= 1000:
        return jsonify({"status": "erro", "message": "limite deve estar entre 1 e 1000"}), 400
    equipamento_id = request.args.get("equipamento_id")

    try:
        with closing(get_connection()) as conn:
            cursor = conn.cursor(dictionary=True)

            query = "SELECT id, equipamento_id, leitura_id, ph, turbidez, temperatura, orp, data_hora FROM leituras"
            params = []
            if equipamento_id is not None:
                query += " WHERE equipamento_id = %s"
                params.append(equipamento_id)
            query += " ORDER BY data_hora DESC, id DESC LIMIT %s"
            params.append(limite)
            cursor.execute(query, tuple(params))

            resultados = cursor.fetchall()
            cursor.close()

        # Evita que o Flask serialize um DATETIME local como GMT.
        for leitura in resultados:
            leitura["data_hora"] = leitura["data_hora"].replace(
                tzinfo=BRAZIL_TIMEZONE
            ).isoformat()

        return jsonify(
            {
                "status": "ok",
                "leituras": resultados
            }
        ), 200

    except mysql.connector.Error as erro:
        return jsonify(
            {
                "status": "erro",
                "message": "Falha ao buscar leituras",
                "detalhe": str(erro)
            }
        ), 500

    except Exception as erro:
        return jsonify(
            {
                "status": "erro",
                "message": "Erro inesperado ao buscar leituras",
                "detalhe": str(erro)
            }
        ), 500


# Rastreador para saber se o schema foi inicializado
_db_schema_initialized = False


def _ensure_db_schema_once():
    """Garante o schema apenas uma vez, usada na primeira requisição."""
    global _db_schema_initialized
    if not _db_schema_initialized:
        try:
            ensure_db_schema()
            _db_schema_initialized = True
        except mysql.connector.Error as error:
            app.logger.error("Falha ao garantir schema do banco: %s", error)
        except ValueError as error:
            app.logger.warning("Variáveis de ambiente não configuradas: %s", error)


@app.before_request
def _init_db_schema_before_request():
    """Hook para garantir o schema antes de qualquer requisição."""
    _ensure_db_schema_once()


# Tentar inicializar o schema no import também, para o Render/gunicorn
try:
    _ensure_db_schema_once()
except Exception as error:
    app.logger.warning("Não foi possível inicializar schema no import: %s", error)


if __name__ == "__main__":
    try:
        ensure_db_schema()
    except mysql.connector.Error as error:
        app.logger.error("Falha na inicialização do banco de dados: %s", error)
        raise

    app.run(host="0.0.0.0", port=int(os.getenv("PORT", 5000)), debug=True)