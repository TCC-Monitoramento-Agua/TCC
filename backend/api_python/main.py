import os
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
                ph FLOAT NOT NULL,
                turbidez FLOAT NOT NULL,
                temperatura FLOAT NOT NULL,
                orp FLOAT NULL,
                data_hora DATETIME NOT NULL
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """
        )
        conn.commit()
        cursor.close()


def ensure_db_schema() -> None:
    try:
        init_db()
    except mysql.connector.Error as error:
        app.logger.error("Falha ao inicializar o banco de dados: %s", error)
        raise


def validar_dados(dados: dict[str, Any]) -> tuple[bool, str]:
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

            cursor.execute(
                """
                INSERT INTO leituras
                (ph, turbidez, temperatura, orp, data_hora)
                VALUES (%s, %s, %s, %s, %s)
                """,
                (ph, turbidez, temperatura, orp, data_hora),
            )

            conn.commit()
            leitura_id = cursor.lastrowid
            cursor.close()

        return jsonify(
            {
                "status": "ok",
                "mensagem": "Leitura salva com sucesso",
                "id": leitura_id
            }
        ), 201

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

    try:
        with closing(get_connection()) as conn:
            cursor = conn.cursor(dictionary=True)

            cursor.execute(
                """
                SELECT id, ph, turbidez, temperatura, orp, data_hora
                FROM leituras
                ORDER BY id DESC
                LIMIT %s
                """,
                (limite,),
            )

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