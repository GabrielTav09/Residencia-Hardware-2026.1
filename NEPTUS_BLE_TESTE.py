"""
=============================================================
  NEPTUS - Testador de Comunicação BLE com ESP32
  Firmware: ESP32-NEPTUS-TURB (codigo_teste_arduino.ino)
=============================================================
  Dependência: pip install bleak
  Uso:         python neptus_ble_tester.py
=============================================================

  DIVERGÊNCIAS ENCONTRADAS: firmware vs documentação
  ─────────────────────────────────────────────────────
  1. Nome BLE real  : "ESP32-NEPTUS-TURB"  (doc dizia "Neptus")
  2. Erro calibração: "ERRO_EM_CALIB"      (doc dizia "ERRO_EM_CALIBRACAO")
  3. Após 500_NTU   : PROCESSAR é disparado automaticamente pelo loop(),
                      NÃO precisa de um 7º CONFIRM — enviar CONFIRM aqui
                      gera ERRO_CONFIRM_FORA_DE_CONTEXTO.
  4. Intervalo JSON : 10 s no firmware de teste (doc diz 2 s na versão final)
  5. turbidez       : sempre 0.0 neste firmware (modo teste sem regressão)
  6. Campo extra    : JSON inclui "tensao" (tensão bruta do sensor ADC)
=============================================================
"""

import asyncio
import json
import sys
from datetime import datetime
from bleak import BleakClient, BleakScanner

# ─────────────────────────────────────────────
#  UUIDs NUS — iguais no firmware e na doc
# ─────────────────────────────────────────────
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # Python → ESP32
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # ESP32 → Python

# ─────────────────────────────────────────────
#  Configurações
# ─────────────────────────────────────────────
DEVICE_NAME_FILTER = "ESP32-NEPTUS-TURB"   # Nome exato definido no BLEDevice::init()
SCAN_TIMEOUT_SEC   = 10
LOG_FILE           = "neptus_ble_log.txt"


# ══════════════════════════════════════════════
#  Utilitários de Log
# ══════════════════════════════════════════════

def log(msg: str, level: str = "INFO"):
    ts   = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    line = f"[{ts}] [{level:<5}] {msg}"
    print(line)
    with open(LOG_FILE, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def log_rx(raw: str):
    """Interpreta e exibe cada mensagem recebida do ESP32."""
    log(f"← ESP32: {raw!r}", "RX")

    # ── Pacote JSON de monitoramento ──────────────────────────────────────
    if raw.strip().startswith("{"):
        try:
            data = json.loads(raw)
            turbidez = data.get("turbidez", "?")
            tensao   = data.get("tensao",   "?")   # campo extra do firmware de teste
            nivel    = data.get("nivel",    "?")
            ts       = data.get("timestamp","?")
            log(
                f"   📊 turbidez={turbidez} NTU | tensao_bruta={tensao:.4f}V | "
                f"nivel='{nivel}' | ts={ts}ms",
                "JSON"
            )
            # Avisa sobre o campo "tensao" que não existe na versão de produção
            if "tensao" in data:
                log("   ℹ  Campo 'tensao' presente — firmware de TESTE (será removido na versão final)", "WARN")
            if turbidez == 0.0:
                log("   ℹ  turbidez=0.0 — regressão polinomial não implementada neste firmware", "WARN")
        except json.JSONDecodeError:
            log("   ⚠  JSON malformado", "WARN")
        return

    # ── Estados de calibração ────────────────────────────────────────────
    STATE_MAP = {
        "0_NTU":   "🟡 Insira o frasco de   0 NTU e envie CONFIRM",
        "100_NTU": "🟡 Troque para o frasco de 100 NTU e envie CONFIRM",
        "200_NTU": "🟡 Troque para o frasco de 200 NTU e envie CONFIRM",
        "300_NTU": "🟡 Troque para o frasco de 300 NTU e envie CONFIRM",
        "400_NTU": "🟡 Troque para o frasco de 400 NTU e envie CONFIRM",
        "500_NTU": "🟡 Troque para o frasco de 500 NTU e envie CONFIRM",
        "LENDO":        "⏳ ESP32 capturando amostras (~800 ms)...",
        "PROCESSANDO":  "⚙️  Calculando regressão polinomial...",
        "CALIB_OK":     "✅ Calibração concluída com sucesso!",
        "CALIB_CANCELADA": "🚫 Calibração cancelada.",
    }
    if raw in STATE_MAP:
        log(f"   {STATE_MAP[raw]}", "STATE")
        return

    # ── Erros — mapeados conforme o firmware real ─────────────────────────
    #   ATENÇÃO: firmware usa "ERRO_EM_CALIB", não "ERRO_EM_CALIBRACAO"
    ERROR_MAP = {
        "ERRO_CONFIRM_FORA_DE_CONTEXTO": (
            "CONFIRM enviado sem calibração ativa (IDLE) "
            "ou após todos os 6 pontos já coletados (estado PROCESSAR)."
        ),
        "ERRO_EM_CALIB": (
            "GET_TURBIDEZ enviado enquanto calibração está ativa. "
            "⚠️  DIVERGÊNCIA: firmware envia 'ERRO_EM_CALIB', "
            "documentação diz 'ERRO_EM_CALIBRACAO' — alinhar com o time."
        ),
    }
    if raw.startswith("ERRO_CMD_DESCONHECIDO:"):
        detalhe = raw.split(":", 1)[1]
        log(f"   ❌ Comando desconhecido recebido pelo ESP32: {detalhe!r} "
            f"(verificar encoding/typo no Frontend)", "ERROR")
        return
    if raw in ERROR_MAP:
        log(f"   ❌ {ERROR_MAP[raw]}", "ERROR")
        return
    if raw.startswith("ERRO_"):
        log(f"   ❌ Erro não mapeado: {raw!r}", "ERROR")
        return

    log(f"   ℹ  Mensagem não reconhecida: {raw!r}", "WARN")


# ══════════════════════════════════════════════
#  Menu interativo
# ══════════════════════════════════════════════

COMMANDS = {
    "1": ("START_CAL",    "Iniciar / Cancelar calibração"),
    "2": ("CONFIRM",      "Confirmar ponto atual"),
    "3": ("GET_TURBIDEZ", "Solicitar leitura instantânea"),
    # Comandos de teste de erro — úteis para validar o firmware
    "4": ("start_cal",    "[TESTE ERRO] minúsculo → ERRO_CMD_DESCONHECIDO"),
    "5": ("STARTCAL",     "[TESTE ERRO] sem underscore → ERRO_CMD_DESCONHECIDO"),
    "6": ("GET_TURBIDEZ", "[TESTE ERRO] enviar GET_TURBIDEZ durante calibração ativa → ERRO_EM_CALIB"),
    "q": (None,           "Sair"),
}

def print_menu():
    print("\n" + "─" * 58)
    print("  NEPTUS BLE TESTER  |  ESP32-NEPTUS-TURB")
    print("─" * 58)
    for key, (cmd, desc) in COMMANDS.items():
        label = cmd if cmd else "─"
        print(f"  [{key}]  {label:<22} {desc}")
    print("─" * 58)
    print("  Ou digite qualquer string manualmente (teste de encoding)")
    print("─" * 58)


# ══════════════════════════════════════════════
#  Scan e seleção de dispositivo
# ══════════════════════════════════════════════

async def scan_and_pick() -> str | None:
    log(f"Escaneando BLE por {SCAN_TIMEOUT_SEC}s (filtro: '{DEVICE_NAME_FILTER}')...")
    devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT_SEC)

    matches = [d for d in devices if DEVICE_NAME_FILTER.lower() in (d.name or "").lower()]

    if not matches:
        log("Dispositivo não encontrado pelo nome. Listando todos:", "WARN")
        all_devs = sorted(devices, key=lambda d: d.name or "")
        for i, d in enumerate(all_devs):
            print(f"  [{i}] {d.address}  {d.name or '(sem nome)'}")
        if not all_devs:
            log("Nenhum dispositivo BLE detectado.", "ERROR")
            return None
        choice = input("Escolha o número do dispositivo (ou Enter para cancelar): ").strip()
        if not choice.isdigit() or int(choice) >= len(all_devs):
            return None
        return all_devs[int(choice)].address

    if len(matches) == 1:
        log(f"Dispositivo encontrado: '{matches[0].name}' [{matches[0].address}]")
        return matches[0].address

    for i, d in enumerate(matches):
        print(f"  [{i}] {d.address}  {d.name}")
    choice = input("Múltiplos encontrados. Escolha o número: ").strip()
    if choice.isdigit() and int(choice) < len(matches):
        return matches[int(choice)].address
    return None


# ══════════════════════════════════════════════
#  Modo 1 — Menu interativo
# ══════════════════════════════════════════════

async def run_interactive(address: str):
    log(f"Conectando em {address}...")

    def on_notify(_sender, data: bytearray):
        try:
            msg = data.decode("utf-8").strip()
        except UnicodeDecodeError:
            msg = data.hex()
        log_rx(msg)

    async with BleakClient(address) as client:
        if not client.is_connected:
            log("Falha na conexão.", "ERROR")
            return

        log(f"Conectado! MTU={client.mtu_size}")
        await client.start_notify(NUS_TX_CHAR_UUID, on_notify)
        log("Notificações TX ativas. Aguardando mensagens do ESP32...")
        log("⚠  Intervalo de monitoramento: 10 s (firmware de teste)", "WARN")

        print_menu()
        loop = asyncio.get_event_loop()

        while True:
            try:
                user_input = await loop.run_in_executor(None, input, "\n> Comando: ")
            except (EOFError, KeyboardInterrupt):
                break

            user_input = user_input.strip()
            if not user_input:
                print_menu()
                continue
            if user_input.lower() == "q":
                break

            # Resolve atalho numérico ou aceita string literal
            if user_input in COMMANDS:
                cmd, _ = COMMANDS[user_input]
                if cmd is None:
                    break
            else:
                cmd = user_input   # permite testar strings brutas (ex: "START_CAL\n")

            log(f"→ Enviando: {cmd!r}", "TX")
            try:
                await client.write_gatt_char(
                    NUS_RX_CHAR_UUID,
                    cmd.encode("utf-8"),
                    response=False,
                )
            except Exception as e:
                log(f"Erro ao enviar: {e}", "ERROR")

            # Aguarda notificações antes do próximo prompt
            # Nota: leituras no ESP32 bloqueiam ~800 ms (ver Ponto Crítico #1 no firmware)
            await asyncio.sleep(1.2)

        await client.stop_notify(NUS_TX_CHAR_UUID)
        log("Desconectado.")


# ══════════════════════════════════════════════
#  Modo 2 — Fluxo assistido de calibração
#
#  Comportamento real do firmware (6 pontos):
#
#  START_CAL → "0_NTU"
#  CONFIRM   → "LENDO" → "100_NTU"
#  CONFIRM   → "LENDO" → "200_NTU"
#  CONFIRM   → "LENDO" → "300_NTU"
#  CONFIRM   → "LENDO" → "400_NTU"
#  CONFIRM   → "LENDO" → "500_NTU"
#  CONFIRM   → "LENDO" → (sem próximo NTU)
#             loop() detecta PROCESSAR → "PROCESSANDO" → "CALIB_OK"
#
#  ⚠  NÃO enviar CONFIRM após "500_NTU" ser processado →
#     resultaria em ERRO_CONFIRM_FORA_DE_CONTEXTO
# ══════════════════════════════════════════════

CALIB_STEPS = ["0_NTU", "100_NTU", "200_NTU", "300_NTU", "400_NTU", "500_NTU"]

async def run_assisted(address: str):
    log("=== MODO ASSISTIDO: Fluxo de Calibração ===")
    log("⚠  Cada leitura bloqueia ~800 ms no ESP32 (Ponto Crítico #1)", "WARN")

    received:      list[str]    = []
    done_event:    asyncio.Event = asyncio.Event()

    def on_notify(_sender, data: bytearray):
        msg = data.decode("utf-8", errors="replace").strip()
        log_rx(msg)
        received.append(msg)
        if msg in ("CALIB_OK", "CALIB_CANCELADA") or msg.startswith("ERRO_"):
            done_event.set()

    async def wait_for(state: str, timeout: float = 15.0) -> bool:
        """Aguarda uma mensagem específica chegar via notificação."""
        elapsed = 0.0
        while state not in received and elapsed < timeout:
            await asyncio.sleep(0.3)
            elapsed += 0.3
        return state in received

    async def send(cmd: str):
        log(f"→ Enviando: {cmd!r}", "TX")
        await client.write_gatt_char(NUS_RX_CHAR_UUID, cmd.encode(), response=False)
        await asyncio.sleep(0.4)

    loop = asyncio.get_event_loop()

    async with BleakClient(address) as client:
        if not client.is_connected:
            log("Falha na conexão.", "ERROR")
            return

        log(f"Conectado! MTU={client.mtu_size}")
        await client.start_notify(NUS_TX_CHAR_UUID, on_notify)

        # ── Passo 0: inicia calibração ────────────────────────────────────
        await send("START_CAL")
        if not await wait_for("0_NTU"):
            log("Timeout: ESP32 não enviou '0_NTU'. Abortando.", "ERROR")
            return

        # ── Passos 1–6: um CONFIRM por frasco ────────────────────────────
        for i, step in enumerate(CALIB_STEPS):
            await loop.run_in_executor(
                None, input,
                f"\n  ▶ [{i+1}/6] Insira o frasco {step} e pressione ENTER para confirmar..."
            )
            await send("CONFIRM")

            # Aguarda "LENDO" (ESP32 inicia captura)
            if not await wait_for("LENDO", timeout=5.0):
                log(f"Aviso: 'LENDO' não recebido após CONFIRM no passo {step}", "WARN")

            # Aguarda próximo passo NTU (exceto após o último)
            if i < len(CALIB_STEPS) - 1:
                next_step = CALIB_STEPS[i + 1]
                log(f"Aguardando '{next_step}'...")
                if not await wait_for(next_step, timeout=10.0):
                    log(f"Timeout aguardando '{next_step}'.", "ERROR")
                    break
            else:
                # Após o 6º CONFIRM o ESP32 entra em PROCESSAR automaticamente
                log("Último ponto confirmado. Aguardando PROCESSANDO + CALIB_OK...")

        # ── Aguarda resultado final ───────────────────────────────────────
        try:
            await asyncio.wait_for(done_event.wait(), timeout=30.0)
        except asyncio.TimeoutError:
            log("Timeout aguardando CALIB_OK. Verifique o Serial Monitor do ESP32.", "ERROR")

        await client.stop_notify(NUS_TX_CHAR_UUID)
        log("Fluxo assistido encerrado.")


# ══════════════════════════════════════════════
#  Entry-point
# ══════════════════════════════════════════════

async def main():
    print("=" * 58)
    print("  NEPTUS BLE Tester  |  firmware: ESP32-NEPTUS-TURB")
    print("=" * 58)
    print(f"  Log salvo em: {LOG_FILE}")
    print("=" * 58)

    address = await scan_and_pick()
    if not address:
        log("Nenhum dispositivo selecionado. Encerrando.", "ERROR")
        sys.exit(1)

    print("\nModo de operação:")
    print("  [1] Interativo  — menu de comandos manuais")
    print("  [2] Assistido   — guia pelo fluxo completo de calibração")
    mode = input("> ").strip()

    if mode == "2":
        await run_assisted(address)
    else:
        await run_interactive(address)


if __name__ == "__main__":
    asyncio.run(main())
