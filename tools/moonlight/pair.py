#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""GameStream/Sunshine pairing + launch client for ProsperoSunshine.

Follows moonlight-qt app/backend/nvpairingmanager.cpp (SHA-256 generation,
appversion >= 7).  The client identity (RSA-2048 key, self-signed cert, unique
id) and the pinned server certificate live in out/moonlight-client/ so later
runs reuse the paired identity.

Commands:
  pair       run the full pairing handshake (sends the PIN to the dev control socket)
  info       /serverinfo + /applist over HTTPS with the client certificate
  cover      /appasset for one app: is it a PNG?
  negative   prove that a fresh, unpaired client certificate is rejected
  launch     /launch (or /resume) over HTTPS, prints JSON for stream_test
  cancel     /cancel over HTTPS
  status     dev control socket STATUS
"""
import argparse
import hashlib
import http.client
import json
import os
import secrets
import socket
import ssl
import sys
import threading
import time
import uuid
import xml.etree.ElementTree as ET
from datetime import datetime, timedelta, timezone
from urllib.parse import urlencode

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.x509.oid import NameOID

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_DIR = os.path.join(REPO, "out", "moonlight-client")


def log(msg):
    print(msg, flush=True)


# ---------------------------------------------------------------- identity

class Identity:
    def __init__(self, directory):
        self.dir = directory
        self.key_path = os.path.join(directory, "client_key.pem")
        self.cert_path = os.path.join(directory, "client_cert.pem")
        self.id_path = os.path.join(directory, "uniqueid.txt")
        self.server_cert_path = os.path.join(directory, "server_cert.pem")

    @staticmethod
    def generate(directory):
        ident = Identity(directory)
        os.makedirs(directory, exist_ok=True)
        if not (os.path.exists(ident.key_path) and os.path.exists(ident.cert_path)):
            key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
            name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "NVIDIA GameStream Client")])
            now = datetime.now(timezone.utc)
            cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
                    .public_key(key.public_key()).serial_number(x509.random_serial_number())
                    .not_valid_before(now - timedelta(days=1))
                    .not_valid_after(now + timedelta(days=365 * 20))
                    .sign(key, hashes.SHA256()))
            with open(ident.key_path, "wb") as f:
                f.write(key.private_bytes(serialization.Encoding.PEM,
                                          serialization.PrivateFormat.PKCS8,
                                          serialization.NoEncryption()))
            with open(ident.cert_path, "wb") as f:
                f.write(cert.public_bytes(serialization.Encoding.PEM))
            with open(ident.id_path, "w") as f:
                f.write(secrets.token_hex(8).upper())
        elif not os.path.exists(ident.id_path):
            with open(ident.id_path, "w") as f:
                f.write(secrets.token_hex(8).upper())
        return ident

    @property
    def uniqueid(self):
        with open(self.id_path) as f:
            return f.read().strip()

    @property
    def cert_pem(self):
        with open(self.cert_path, "rb") as f:
            return f.read()

    @property
    def key(self):
        with open(self.key_path, "rb") as f:
            return serialization.load_pem_private_key(f.read(), None)

    @property
    def cert(self):
        return x509.load_pem_x509_certificate(self.cert_pem)

    def pinned_server_cert(self):
        if os.path.exists(self.server_cert_path):
            with open(self.server_cert_path, "rb") as f:
                return x509.load_pem_x509_certificate(f.read())
        return None


# ---------------------------------------------------------------- crypto

def sha256(b):
    return hashlib.sha256(b).digest()


def aes_ecb(key, data, encrypt):
    c = Cipher(algorithms.AES(key), modes.ECB())
    op = c.encryptor() if encrypt else c.decryptor()
    return op.update(data) + op.finalize()


def cert_signature(cert):
    """Raw signature bytes of an X.509 certificate (what Sunshine/moonlight hash)."""
    return cert.signature


# ---------------------------------------------------------------- HTTP

class Host:
    def __init__(self, addr, http_port=47989, https_port=47984, ctl_port=47995):
        self.addr = addr
        self.http_port = http_port
        self.https_port = https_port
        self.ctl_port = ctl_port

    def http_get(self, path, params, timeout=60):
        conn = http.client.HTTPConnection(self.addr, self.http_port, timeout=timeout)
        conn.request("GET", path + "?" + urlencode(params))
        r = conn.getresponse()
        body = r.read()
        conn.close()
        return r.status, body

    def https_get(self, ident, path, params, timeout=60, pin_server=True):
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        ctx.load_cert_chain(ident.cert_path, ident.key_path)
        conn = http.client.HTTPSConnection(self.addr, self.https_port, timeout=timeout, context=ctx)
        conn.request("GET", path + "?" + urlencode(params))
        # pin the server certificate learned during pairing
        der = conn.sock.getpeercert(binary_form=True)
        pinned = ident.pinned_server_cert() if pin_server else None
        if pinned is not None and der != pinned.public_bytes(serialization.Encoding.DER):
            raise RuntimeError("HTTPS server certificate does not match the one pinned at pairing")
        r = conn.getresponse()
        body = r.read()
        conn.close()
        return r.status, body

    def control(self, line, until_end=False, timeout=5):
        s = socket.create_connection((self.addr, self.ctl_port), timeout=timeout)
        s.sendall(line.encode() + b"\n")
        buf = b""
        try:
            while True:
                d = s.recv(4096)
                if not d:
                    break
                buf += d
                if not until_end and b"\n" in buf:
                    break
                if until_end and buf.rstrip().endswith(b"END"):
                    break
        except socket.timeout:
            pass
        s.close()
        return buf.decode(errors="replace")


def parse_xml(body):
    return ET.fromstring(body)


def xml_text(root, tag, default=None):
    e = root.find(tag)
    return e.text if e is not None and e.text is not None else default


def base_params(ident):
    return {"uniqueid": ident.uniqueid, "uuid": str(uuid.uuid4())}


def check_status(root, what):
    code = root.get("status_code")
    if code != "200":
        raise RuntimeError("%s: status_code=%s message=%r" % (what, code, root.get("status_message")))


# ---------------------------------------------------------------- pairing

def do_pair(host, ident, pin, name, auto_pin=True, verbose=True, host_pin=None, strict=True):
    def v(m):
        if verbose:
            log(m)

    salt = secrets.token_bytes(16)
    aes_key = sha256(salt + pin.encode())[:16]
    client_cert_pem = ident.cert_pem

    def pr(extra):
        return {**base_params(ident), "devicename": name, "updateState": "1", **extra}

    # 1. getservercert -- blocks until the PIN is approved on the host
    result = {}

    def req_servercert():
        t0 = time.time()
        try:
            result["resp"] = host.http_get("/pair", pr({
                "phrase": "getservercert", "salt": salt.hex(),
                "clientcert": client_cert_pem.hex()}), timeout=120)
        except Exception as e:  # noqa
            result["err"] = e
        result["dt"] = time.time() - t0

    th = threading.Thread(target=req_servercert, daemon=True)
    th.start()
    if auto_pin:
        # wait until the host lists the pending request, then approve it
        deadline = time.time() + 15
        pend = ""
        while time.time() < deadline:
            pend = host.control("PAIRINGS", until_end=True)
            if any(l.strip() and l.strip() != "END" for l in pend.splitlines()):
                break
            time.sleep(0.3)
        v("[1] control PAIRINGS -> %r" % pend)
        # PIN answers only when the whole handshake has finished, so it runs beside the steps below.
        pin_result = {}

        def submit_pin():
            try:
                pin_result["resp"] = host.control("PIN %s %s" % (host_pin or pin, name), timeout=60)
            except Exception as e:  # noqa
                pin_result["resp"] = "error: %r" % (e,)
            v("[1] control PIN -> %r" % pin_result["resp"].strip())

        threading.Thread(target=submit_pin, daemon=True).start()
    else:
        v("[1] waiting for the PIN %s to be entered on the host ..." % pin)
    th.join(120)
    if "err" in result:
        raise RuntimeError("getservercert request failed: %r" % (result["err"],))
    status, body = result["resp"]
    v("[1] getservercert HTTP %d after %.2fs: %s" % (status, result["dt"], body.decode(errors="replace")[:200] + " ..."))
    root = parse_xml(body)
    check_status(root, "getservercert")
    if xml_text(root, "paired") != "1":
        raise RuntimeError("getservercert: paired != 1")
    server_cert_pem = bytes.fromhex(xml_text(root, "plaincert"))
    server_cert = x509.load_pem_x509_certificate(server_cert_pem)
    v("[1] server cert subject=%s" % server_cert.subject.rfc4514_string())

    # 2. client challenge
    rnd_challenge = secrets.token_bytes(16)
    status, body = host.http_get("/pair", pr({"clientchallenge": aes_ecb(aes_key, rnd_challenge, True).hex()}))
    v("[2] clientchallenge HTTP %d: %s" % (status, body.decode(errors="replace")[:300]))
    root = parse_xml(body)
    check_status(root, "clientchallenge")
    if xml_text(root, "paired") != "1":
        raise RuntimeError("clientchallenge: paired != 1")
    resp = aes_ecb(aes_key, bytes.fromhex(xml_text(root, "challengeresponse")), False)
    server_resp_hash, server_challenge = resp[:32], resp[32:48]

    # 3. server challenge response
    client_secret = secrets.token_bytes(16)
    h = sha256(server_challenge + cert_signature(ident.cert) + client_secret)
    status, body = host.http_get("/pair", pr({"serverchallengeresp": aes_ecb(aes_key, h, True).hex()}))
    v("[3] serverchallengeresp HTTP %d: %s" % (status, body.decode(errors="replace")[:400]))
    root = parse_xml(body)
    check_status(root, "serverchallengeresp")
    if xml_text(root, "paired") != "1":
        raise RuntimeError("serverchallengeresp: paired != 1 (wrong PIN?)")
    pairing_secret = bytes.fromhex(xml_text(root, "pairingsecret"))
    server_secret, server_sig = pairing_secret[:16], pairing_secret[16:]
    try:
        server_cert.public_key().verify(server_sig, server_secret, padding.PKCS1v15(), hashes.SHA256())
        v("[3] server signature over pairing secret: VALID")
    except Exception:
        raise RuntimeError("server signature invalid (MITM?)")
    expected = sha256(rnd_challenge + cert_signature(server_cert) + server_secret)
    if expected != server_resp_hash:
        if strict:
            raise RuntimeError("server challenge response hash mismatch (wrong PIN or MITM)")
        v("[3] server challenge response hash MISMATCH (wrong PIN) -- continuing anyway (--no-verify)")
    else:
        v("[3] server challenge response hash: MATCH (PIN accepted by server)")

    # 4. client pairing secret
    sig = ident.key.sign(client_secret, padding.PKCS1v15(), hashes.SHA256())
    status, body = host.http_get("/pair", pr({"clientpairingsecret": (client_secret + sig).hex()}))
    v("[4] clientpairingsecret HTTP %d: %s" % (status, body.decode(errors="replace")[:300]))
    root = parse_xml(body)
    check_status(root, "clientpairingsecret")
    if xml_text(root, "paired") != "1":
        raise RuntimeError("clientpairingsecret: paired != 1 (host rejected the pairing)")

    # pin the server cert so later HTTPS requests can verify it
    with open(ident.server_cert_path, "wb") as f:
        f.write(server_cert_pem)

    # 5. pairchallenge over HTTPS with the client cert
    status, body = host.https_get(ident, "/pair", pr({"phrase": "pairchallenge"}))
    v("[5] pairchallenge (HTTPS) HTTP %d: %s" % (status, body.decode(errors="replace")[:300]))
    root = parse_xml(body)
    check_status(root, "pairchallenge")
    if xml_text(root, "paired") != "1":
        raise RuntimeError("pairchallenge: paired != 1")
    return True


# ---------------------------------------------------------------- commands

def cmd_pair(a):
    ident = Identity.generate(a.dir)
    host = Host(a.host)
    pin = a.pin or "%04d" % secrets.randbelow(10000)
    log("client uniqueid=%s identity dir=%s PIN=%s" % (ident.uniqueid, a.dir, pin))
    do_pair(host, ident, pin, a.name, auto_pin=not a.manual_pin, host_pin=a.host_pin, strict=not a.no_verify)
    log("PAIRING OK")
    return cmd_info(a)


def cmd_info(a):
    ident = Identity.generate(a.dir)
    host = Host(a.host)
    status, body = host.https_get(ident, "/serverinfo", base_params(ident))
    log("GET https://%s:%d/serverinfo -> HTTP %d" % (a.host, host.https_port, status))
    root = parse_xml(body)
    info = {c.tag: c.text for c in root}
    for k in ("hostname", "appversion", "GfeVersion", "ServerCodecModeSupport", "PairStatus",
              "currentgame", "state", "uniqueid", "mac"):
        log("  %s = %s" % (k, info.get(k)))
    status, body = host.https_get(ident, "/applist", base_params(ident))
    log("GET https://%s:%d/applist -> HTTP %d" % (a.host, host.https_port, status))
    log(body.decode(errors="replace"))
    root = parse_xml(body)
    for app in root.findall("App"):
        log("  app: id=%s title=%r hdr=%s" % (xml_text(app, "ID"), xml_text(app, "AppTitle"),
                                              xml_text(app, "IsHdrSupported")))
    ok = info.get("PairStatus") == "1" and root.get("status_code") == "200"
    log("RESULT: PairStatus=%s applist_status=%s -> %s" % (info.get("PairStatus"), root.get("status_code"),
                                                          "OK" if ok else "FAIL"))
    return 0 if ok else 1


def served(status, body):
    return status == 200 and isinstance(body, bytes) and b'status_code="200"' in body


def try_request(host, ident, path, params, label, cert=True):
    try:
        if cert:
            status, body = host.https_get(ident, path, params, pin_server=False)
        else:
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            conn = http.client.HTTPSConnection(host.addr, host.https_port, timeout=10, context=ctx)
            conn.request("GET", path + "?" + urlencode(params))
            r = conn.getresponse()
            status, body = r.status, r.read()
        log("  %s -> HTTP %d %s" % (label, status, body.decode(errors="replace")[:200].replace("\n", " ")))
        return status, body
    except Exception as e:
        log("  %s -> %s: %s" % (label, type(e).__name__, e))
        return None, str(e)


def cmd_negative(a):
    host = Host(a.host)
    tmp = os.path.join(a.dir, "unpaired")
    for f in ("client_key.pem", "client_cert.pem", "uniqueid.txt"):
        p = os.path.join(tmp, f)
        if os.path.exists(p):
            os.remove(p)
    ident = Identity.generate(tmp)  # always a fresh, never-paired identity
    log("fresh unpaired client uniqueid=%s" % ident.uniqueid)
    bad = 0
    for path, extra, label in [
        ("/applist", {}, "HTTPS /applist (fresh cert)"),
        ("/launch", {"appid": "1", "mode": "1920x1080x60", "rikey": "00" * 16, "rikeyid": "1",
                     "localAudioPlayMode": "0", "sops": "0"}, "HTTPS /launch (fresh cert)"),
        ("/cancel", {}, "HTTPS /cancel (fresh cert)"),
    ]:
        status, body = try_request(host, ident, path, {**base_params(ident), **extra}, label)
        if served(status, body):
            bad += 1
    status, body = try_request(host, ident, "/applist", base_params(ident),
                               "HTTPS /applist (no client cert at all)", cert=False)
    if served(status, body):
        bad += 1
    s, b = host.http_get("/serverinfo", base_params(ident))
    log("  HTTP /serverinfo (fresh uniqueid) -> HTTP %d PairStatus=%s" % (
        s, xml_text(parse_xml(b), "PairStatus")))
    log("RESULT: %s" % ("FAIL: unpaired client was served" if bad else "OK: unpaired client rejected everywhere"))
    return 1 if bad else 0


def cmd_launch(a):
    ident = Identity.generate(a.dir)
    host = Host(a.host)
    rikey = a.rikey or secrets.token_hex(16)
    rikeyid = a.rikeyid if a.rikeyid is not None else secrets.randbelow(2**31)
    params = {**base_params(ident), "appid": str(a.appid), "mode": a.mode, "additionalStates": "1",
              "sops": "0", "rikey": rikey, "rikeyid": str(rikeyid), "localAudioPlayMode": "0",
              "surroundAudioInfo": "196610", "remoteControllerBitmap": "1", "gcmap": "1"}
    if a.corever:
        params["corever"] = "1"
    st, sb = host.https_get(ident, "/serverinfo", base_params(ident))
    sroot = parse_xml(sb)
    check_status(sroot, "serverinfo")
    verb = "/resume" if a.resume else "/launch"
    status, body = host.https_get(ident, verb, params)
    sys.stderr.write("GET %s -> HTTP %d\n%s\n" % (verb, status, body.decode(errors="replace")))
    root = parse_xml(body)
    check_status(root, verb)
    out = {"sessionUrl0": xml_text(root, "sessionUrl0"), "rikey": rikey, "rikeyid": rikeyid,
           "gamesession": xml_text(root, "gamesession"),
           "appversion": xml_text(sroot, "appversion"), "gfe": xml_text(sroot, "GfeVersion", ""),
           "codec_mode": int(xml_text(sroot, "ServerCodecModeSupport", "1"))}
    print(json.dumps(out))
    return 0


def cmd_cancel(a):
    ident = Identity.generate(a.dir)
    host = Host(a.host)
    status, body = host.https_get(ident, "/cancel", base_params(ident))
    log("GET /cancel -> HTTP %d %s" % (status, body.decode(errors="replace")))
    return 0 if status == 200 else 1


def cmd_status(a):
    log(Host(a.host).control("STATUS").strip())
    return 0


def cmd_cover(a):
    ident = Identity.generate(a.dir)
    host = Host(a.host)
    status, body = host.https_get(ident, "/appasset", {**base_params(ident), "appid": str(a.appid), "AssetType": "2",
                                                       "AssetIdx": "0"})
    png = body.startswith(b"\x89PNG\r\n\x1a\n")
    log("GET /appasset appid=%d -> HTTP %d, %d bytes, png=%s" % (a.appid, status, len(body), png))
    if a.out and status == 200:
        with open(a.out, "wb") as f:
            f.write(body)
    return 0 if status == 200 and png else 1


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default=os.environ.get("PS5_HOST"), required="PS5_HOST" not in os.environ)
    p.add_argument("--dir", default=DEFAULT_DIR, help="client identity directory")
    sub = p.add_subparsers(dest="cmd", required=True)
    sp = sub.add_parser("pair")
    sp.add_argument("--pin")
    sp.add_argument("--name", default="moonlight-test")
    sp.add_argument("--no-verify", action="store_true",
                    help="do not abort on a PIN mismatch seen by the client (wrong-PIN test: see what the host does)")
    sp.add_argument("--host-pin", help="PIN sent to the host control socket if different from --pin (wrong-PIN test)")
    sp.add_argument("--manual-pin", action="store_true",
                    help="do not use the dev control socket; wait for the PIN to be entered on the host")
    sub.add_parser("info")
    sub.add_parser("negative")
    sl = sub.add_parser("launch")
    sl.add_argument("--appid", type=int, default=1)
    sl.add_argument("--mode", default="1920x1080x60")
    sl.add_argument("--rikey")
    sl.add_argument("--rikeyid", type=int)
    sl.add_argument("--corever", action="store_true", help="add corever=1 (encrypted RTSP, like real Moonlight)")
    sl.add_argument("--resume", action="store_true")
    sub.add_parser("cancel")
    sub.add_parser("status")
    sc = sub.add_parser("cover")
    sc.add_argument("--appid", type=int, default=1)
    sc.add_argument("--out", help="save the picture here")
    a = p.parse_args()
    fn = {"pair": cmd_pair, "info": cmd_info, "negative": cmd_negative, "launch": cmd_launch,
          "cancel": cmd_cancel, "status": cmd_status, "cover": cmd_cover}[a.cmd]
    try:
        sys.exit(fn(a) or 0)
    except Exception as e:
        log("ERROR: %s: %s" % (type(e).__name__, e))
        sys.exit(1)


if __name__ == "__main__":
    main()
