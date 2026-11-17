#!/usr/bin/env python3
"""Sign Stella firmware for over-the-air updates.

  keygen   make an ECDSA P-256 key pair (keep the private key OFFLINE and out of git)
  sign     write manifest.json for a firmware image
  verify   check a manifest against an image and a public key
  message  print the digests a device computes (used by the tests)

The device downloads manifest.json, streams the image while hashing it, and
accepts it only if the size and SHA-256 match and the signature over
SHA-256("stella-update-v1|<version>|<hex sha256 of the image>") verifies with
the public key built into the firmware. See docs/ARCHITECTURE.md (Updates).

Needs:  pip install cryptography
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature
    from cryptography.exceptions import InvalidSignature
except ImportError:  # pragma: no cover
    sys.exit("This tool needs the 'cryptography' package: pip install cryptography")

PREFIX = "stella-update-v1|"


def image_digest(image: bytes) -> str:
    return hashlib.sha256(image).hexdigest()


def signed_message(version: str, image_hex: str) -> bytes:
    return f"{PREFIX}{version}|{image_hex}".encode()


def signed_digest(version: str, image_hex: str) -> str:
    return hashlib.sha256(signed_message(version, image_hex)).hexdigest()


def cmd_keygen(args):
    private = ec.generate_private_key(ec.SECP256R1())
    Path(args.private).write_bytes(
        private.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    )
    Path(args.private).chmod(0o600)
    pem = private.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
    Path(args.public).write_bytes(pem)
    lines = pem.decode().strip().splitlines()
    literal = "\n".join(f'    "{line}\\n"' for line in lines)
    print(f"private key: {args.private} (keep it offline; never commit it)")
    print(f"public key:  {args.public}\n\nPaste into examples/CYD/secrets.h:\n")
    print("#define STELLA_UPDATE_PUBLIC_KEY \\")
    print(literal)


def cmd_sign(args):
    image = Path(args.image).read_bytes()
    key = serialization.load_pem_private_key(Path(args.key).read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        sys.exit("the key must be an ECDSA P-256 private key")
    if not args.url.startswith("https://"):
        sys.exit("the image URL must be https://")
    if len(args.url) > 95:
        sys.exit("the image URL must be at most 95 characters (the device's JSON string limit)")
    digest = image_digest(image)
    r, s = decode_dss_signature(key.sign(signed_message(args.version, digest), ec.ECDSA(hashes.SHA256())))
    manifest = {
        "version": args.version,
        "url": args.url,
        "size": len(image),
        "sha256": digest,
        "sig_r": f"{r:064x}",
        "sig_s": f"{s:064x}",
    }
    Path(args.out).write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {args.out}: stella {args.version}, {len(image)} bytes, sha256 {digest[:16]}...")


def cmd_verify(args):
    manifest = json.loads(Path(args.manifest).read_text())
    image = Path(args.image).read_bytes()
    problems = []
    if manifest["size"] != len(image):
        problems.append("size differs from the image")
    if manifest["sha256"] != image_digest(image):
        problems.append("sha256 differs from the image")
    pub = serialization.load_pem_public_key(Path(args.public).read_bytes())
    signature = encode_dss_signature(int(manifest["sig_r"], 16), int(manifest["sig_s"], 16))
    try:
        pub.verify(signature, signed_message(manifest["version"], manifest["sha256"]), ec.ECDSA(hashes.SHA256()))
    except InvalidSignature:
        problems.append("the signature does not verify")
    if problems:
        sys.exit("NOT OK: " + "; ".join(problems))
    print("OK: size, sha256 and signature all verify")


def cmd_message(args):
    image = Path(args.image).read_bytes() if args.image else args.text.encode()
    digest = image_digest(image)
    print(f"image sha256   {digest}")
    print(f"signed digest  {signed_digest(args.version, digest)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("keygen")
    p.add_argument("--private", required=True)
    p.add_argument("--public", required=True)
    p.set_defaults(fn=cmd_keygen)
    p = sub.add_parser("sign")
    p.add_argument("--key", required=True)
    p.add_argument("--image", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--url", required=True)
    p.add_argument("--out", default="manifest.json")
    p.set_defaults(fn=cmd_sign)
    p = sub.add_parser("verify")
    p.add_argument("--public", required=True)
    p.add_argument("--manifest", required=True)
    p.add_argument("--image", required=True)
    p.set_defaults(fn=cmd_verify)
    p = sub.add_parser("message")
    p.add_argument("--version", required=True)
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--image")
    g.add_argument("--text")
    p.set_defaults(fn=cmd_message)
    args = parser.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
