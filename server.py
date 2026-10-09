# Vinicius Montuani, Vitor Gaspar, Vitor Augusto, Luiz Leite

from flask import Flask, request, jsonify
import sqlite3

from datetime import datetime

app = Flask(__name__)

# Nome do banco de dados SQLite
DB_NAME = "estufa.db"

def init_db():
    """Inicializa o banco de dados e cria a tabela de telemetria se não existir"""
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS telemetria (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT,
            placa TEXT,
            seq INTEGER,
            temp REAL,
            umid REAL,
            janela BOOLEAN,
            luz_pct INTEGER,
            estado INTEGER,
            timestamp TEXT
        )
    ''')
    conn.commit()
    conn.close()

@app.route('/api/telemetria', methods=['POST'])
def receber_telemetria():
    """Endpoint que o Arduino vai chamar via POST"""
    dados = request.get_json()
    
    if not dados:
        return jsonify({"erro": "JSON inválido ou ausente"}), 400
    
    # Extrai os dados enviados pelo Arduino
    device_id = dados.get("device_id")
    placa = dados.get("placa")
    seq = dados.get("seq")
    temp = dados.get("temp")
    umid = dados.get("umid")
    janela = dados.get("janela")
    luz_pct = dados.get("luz_pct")
    estado = dados.get("estado")
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # Validação simples de acordo com os requisitos da prova (ex: temperatura -10 a 60, umidade 0 a 100)
    if not (-10 <= temp <= 60) or not (0 <= umid <= 100):
        return jsonify({"erro": "Dados fora do intervalo válido, descartados."}), 400

    # Salva no Banco de Dados SQLite
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    cursor.execute('''
        INSERT INTO telemetria (device_id, placa, seq, temp, umid, janela, luz_pct, estado, timestamp)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''', (device_id, placa, seq, temp, umid, janela, luz_pct, estado, timestamp))
    conn.commit()
    conn.close()

    print(f"[{timestamp}] Recebido ID: {device_id} | Seq: {seq} | Temp: {temp}°C | Umid: {umid}% | Estado: {estado}")
    return jsonify({"status": "sucesso", "mensagem": "Telemetria salva com sucesso!"}), 200

@app.route('/', methods=['GET'])
def painel_simples():
    """Página simples para visualizar a última leitura no navegador"""
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    cursor.execute('SELECT * FROM telemetria ORDER BY id DESC LIMIT 1')
    ultima = cursor.fetchone()
    conn.close()

    if not ultima:
        return "<h1>EstufaSmart - Painel</h1><p>Nenhum dado recebido ainda.</p>"

    html = f"""
    <html>
        <head>
            <title>EstufaSmart - Painel</title>
            <meta http-equiv="refresh" content="5"> <!-- Atualiza a página a cada 5 segundos -->
            <style>
                body {{ font-family: Arial; text-align: center; margin-top: 50px; background: #f4f4f9; }}
                .card {{ background: white; padding: 20px; border-radius: 10px; display: inline-block; box-shadow: 0px 0px 10px rgba(0,0,0,0.1); }}
            </style>
        </head>
        <body>
            <div class="card">
                <h1>EstufaSmart - Monitoramento</h1>
                <p><b>Última Atualização:</b> {ultima[9]}</p>
                <hr>
                <p><b>Temperatura:</b> {ultima[4]} °C</p>
                <p><b>Umidade:</b> {ultima[5]} %</p>
                <p><b>Luminosidade:</b> {ultima[7]} %</p>
                <p><b>Janela Aberta:</b> {'Sim' if ultima[6] else 'Não'}</p>
                <p><b>Estado Atual:</b> {ultima[8]}</p>
            </div>
        </body>
    </html>
    """
    return html

if __name__ == '__main__':
    init_db()
    # Roda na porta 5000 escutando em todas as interfaces da rede local
    app.run(host='0.0.0.0', port=5000, debug=True)