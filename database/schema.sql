CREATE DATABASE IF NOT EXISTS monitoramento_agua;
USE monitoramento_agua;

CREATE TABLE IF NOT EXISTS leituras (
    id INT AUTO_INCREMENT PRIMARY KEY,
    equipamento_id VARCHAR(64) NULL,
    ph FLOAT NOT NULL,
    turbidez FLOAT NOT NULL,
    temperatura FLOAT NOT NULL,
    orp FLOAT NULL,
    data_hora DATETIME NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
