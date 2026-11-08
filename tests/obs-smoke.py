"""Opt-in smoke test for the isolated portable OBS fixture on localhost.

Uses only the Python standard library. Never point this at a production OBS:
it creates test scenes and changes Program/Preview in the target instance.
"""
import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import time


class OBSClient:
    def __init__(self, port, password):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((
            f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
        ).encode())
        header = bytearray()
        while not header.endswith(b"\r\n\r\n"):
            header.extend(self.exact(1))
            if len(header) > 16384:
                raise RuntimeError("Oversized HTTP upgrade response")
        expected = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        if not header.startswith(b"HTTP/1.1 101") or expected.lower() not in bytes(header).lower():
            raise RuntimeError("Invalid WebSocket upgrade")
        hello = self.receive()
        identify = {"rpcVersion": 1, "eventSubscriptions": 0}
        auth = hello["d"].get("authentication")
        if auth:
            secret = base64.b64encode(hashlib.sha256((password + auth["salt"]).encode()).digest()).decode()
            identify["authentication"] = base64.b64encode(hashlib.sha256((secret + auth["challenge"]).encode()).digest()).decode()
        self.send({"op": 1, "d": identify})
        if self.receive()["op"] != 2:
            raise RuntimeError("OBS identification failed")
        self.counter = 0

    def exact(self, count):
        result = bytearray()
        while len(result) < count:
            block = self.sock.recv(count - len(result))
            if not block:
                raise RuntimeError("OBS closed the connection")
            result.extend(block)
        return bytes(result)

    def frame(self, payload, opcode=1):
        mask = os.urandom(4)
        length = len(payload)
        if length < 126:
            header = bytes((0x80 | opcode, 0x80 | length))
        elif length < 65536:
            header = bytes((0x80 | opcode, 0x80 | 126)) + struct.pack("!H", length)
        else:
            header = bytes((0x80 | opcode, 0x80 | 127)) + struct.pack("!Q", length)
        masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def send(self, message):
        self.frame(json.dumps(message).encode())

    def receive(self):
        message = bytearray()
        while True:
            first, second = self.exact(2)
            length = second & 127
            if length == 126:
                length = struct.unpack("!H", self.exact(2))[0]
            elif length == 127:
                length = struct.unpack("!Q", self.exact(8))[0]
            if length > 8 * 1024 * 1024:
                raise RuntimeError("Oversized WebSocket message")
            if second & 128:
                raise RuntimeError("Unexpected masked server frame")
            payload = self.exact(length)
            opcode = first & 15
            if opcode == 8:
                raise RuntimeError("OBS closed WebSocket")
            if opcode == 9:
                self.frame(payload, 10)
                continue
            if opcode == 10:
                continue
            message.extend(payload)
            if first & 128:
                return json.loads(message)

    def call(self, name, **fields):
        self.counter += 1
        request_id = str(self.counter)
        self.send({"op": 6, "d": {"requestType": name, "requestId": request_id, "requestData": fields}})
        while True:
            response = self.receive()
            if response["op"] == 7 and response["d"]["requestId"] == request_id:
                response = response["d"]
                if not response["requestStatus"]["result"]:
                    raise RuntimeError(f"{name}: {response['requestStatus']}")
                return response.get("responseData", {})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=4469)
    parser.add_argument("--password", default="scene-deck-isolated-smoke-test")
    parser.add_argument("--isolated-fixture", action="store_true", required=True,
                        help="Confirm that this is the project-local disposable OBS fixture")
    args = parser.parse_args()
    obs = OBSClient(args.port, args.password)
    names = {scene["sceneName"] for scene in obs.call("GetSceneList")["scenes"]}
    for index in range(1, 9):
        name = f"Scene Deck Test {index}"
        if name not in names:
            obs.call("CreateScene", sceneName=name)
            obs.call("CreateInput", sceneName=name, inputName=f"Deck Color {index}",
                     inputKind="color_source_v3", inputSettings={"color": 0xFF000000 | (index * 0x1C2040),
                                                               "width": 1280, "height": 720}, sceneItemEnabled=True)
    obs.call("SetStudioModeEnabled", studioModeEnabled=True)
    obs.call("SetCurrentProgramScene", sceneName="Scene Deck Test 1")
    obs.call("SetCurrentPreviewScene", sceneName="Scene Deck Test 2")
    time.sleep(0.5)
    scene = next(scene for scene in obs.call("GetSceneList")["scenes"] if scene["sceneName"] == "Scene Deck Test 3")
    obs.call("TriggerHotkeyByName", hotkeyName="SceneDeck.Scene." + scene["sceneUuid"])
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if obs.call("GetCurrentPreviewScene")["sceneName"] == "Scene Deck Test 3":
            break
        time.sleep(0.1)
    else:
        raise RuntimeError("Native plugin hotkey did not send scene 3 to Preview")
    if obs.call("GetCurrentProgramScene")["sceneName"] != "Scene Deck Test 1":
        raise RuntimeError("Safe-mode hotkey changed Program")
    print("PASS: real OBS loaded scene actions; native hotkey changed Preview and preserved Program")
    obs.sock.close()


if __name__ == "__main__":
    main()
