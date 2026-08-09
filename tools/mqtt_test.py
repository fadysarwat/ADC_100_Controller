import hmac
import hashlib
import time
import json
import sys
import paho.mqtt.client as mqtt

# ── Config ──
BROKER   = "e1b91c9a41344a7280a17cf27e8b3c3d.s1.eu.hivemq.cloud"
PORT     = 8883
USERNAME = "adc100"
PASSWORD = "!YANKFrnQH6wkrV"
DEVICE   = "ctrl_001"
SECRET   = "ADC100_MQTT_SECRET_KEY_2024"
TOPIC    = f"adc100/{DEVICE}/cmd"
EVENT    = f"adc100/{DEVICE}/event"

def generate_cmd(payload: dict) -> dict:
    cmd = payload["cmd"]
    ts  = int(time.time())
    message = f"{cmd}:{ts}"
    sig = hmac.new(SECRET.encode(), message.encode(), hashlib.sha256).hexdigest()
    payload["ts"]   = ts
    payload["hmac"] = sig
    return payload

def on_connect(client, userdata, flags, rc):
    if rc != 0:
        print(f"[-] Connection failed: rc={rc}")
        return
    print(f"[+] Connected to HiveMQ")
    client.subscribe(EVENT)
    mode = client._userdata["mode"]

    if mode == "send":
        payload = generate_cmd({"cmd": "open"})
        print(f"[+] Sending: {json.dumps(payload)}")
        client.publish(TOPIC, json.dumps(payload))

    elif mode == "replay":
        ts  = int(time.time()) - 60
        cmd = "open"
        sig = hmac.new(SECRET.encode(), f"{cmd}:{ts}".encode(), hashlib.sha256).hexdigest()
        payload = {"cmd": cmd, "ts": ts, "hmac": sig}
        print(f"[+] Replaying old command: {json.dumps(payload)}")
        print(f"[!] Expected: REJECTED (ts too old)")
        client.publish(TOPIC, json.dumps(payload))

    elif mode == "tamper":
        payload = generate_cmd({"cmd": "open"})
        payload["hmac"] = "a" * 64
        print(f"[+] Sending tampered HMAC: {json.dumps(payload)}")
        print(f"[!] Expected: REJECTED (invalid HMAC)")
        client.publish(TOPIC, json.dumps(payload))

    elif mode == "add_rfid":
        payload = generate_cmd({
            "cmd":       "add_credential",
            "cred_type": "rfid",
            "cred_data": "AABBCCDD",
            "label":     "card_test",
            "duress":    "false"
        })
        print(f"[+] Adding RFID credential: {json.dumps(payload)}")
        client.publish(TOPIC, json.dumps(payload))

    elif mode == "add_pin":
        payload = generate_cmd({
            "cmd":       "add_credential",
            "cred_type": "pin",
            "cred_data": "5678",
            "label":     "pin_test",
            "duress":    "false"
        })
        print(f"[+] Adding PIN credential: {json.dumps(payload)}")
        client.publish(TOPIC, json.dumps(payload))

    elif mode == "add_duress":
        payload = generate_cmd({
            "cmd":       "add_credential",
            "cred_type": "pin",
            "cred_data": "0000",
            "label":     "pin_duress_test",
            "duress":    "true"
        })
        print(f"[+] Adding duress PIN: {json.dumps(payload)}")
        client.publish(TOPIC, json.dumps(payload))

    elif mode == "delete_rfid":
        payload = generate_cmd({
            "cmd":   "delete_credential",
            "label": "card_test"
        })
        print(f"[+] Deleting RFID credential: {json.dumps(payload)}")
        client.publish(TOPIC, json.dumps(payload))

    elif mode == "delete_pin":
        payload = generate_cmd({
            "cmd":   "delete_credential",
            "label": "pin_test"
        })
        print(f"[+] Deleting PIN credential: {json.dumps(payload)}")
        client.publish(TOPIC, json.dumps(payload))

    elif mode == "clear":
        payload = generate_cmd({"cmd": "clear_credentials"})
        print(f"[+] Clearing all credentials")
        print(f"[!] WARNING: This will delete ALL credentials!")
        client.publish(TOPIC, json.dumps(payload))

def on_message(client, userdata, msg):
    print(f"[<] Event received: {msg.payload.decode()}")
    client.disconnect()

def on_publish(client, userdata, mid):
    print(f"[+] Published — waiting for device response...")

MODES = ["send", "replay", "tamper", "add_rfid", "add_pin", "add_duress",
         "delete_rfid", "delete_pin", "clear"]

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in MODES:
        print(f"Usage: python mqtt_test.py [{' | '.join(MODES)}]")
        sys.exit(1)

    mode = sys.argv[1]
    client = mqtt.Client(userdata={"mode": mode})
    client.username_pw_set(USERNAME, PASSWORD)
    client.tls_set()
    client.on_connect  = on_connect
    client.on_message  = on_message
    client.on_publish  = on_publish
    client.connect(BROKER, PORT)
    client.loop_forever()
