#!/usr/bin/env python3
"""Make the update manifest for a Stella firmware image.

  manifest  write manifest.json (version, URL, size, SHA-256): all that is
            needed for updates; the CI workflow runs this on every merge
  keygen    (optional) make an ECDSA P-256 key pair for signed updates
  sign      (optional) like manifest, plus an ECDSA signature
  verify    check a signed manifest against an image and a public key
  message   print the digests a device computes (used by the tests)

The device downloads manifest.json over HTTPS, streams the image while
hashing it, and installs it only if the size and SHA-256 match. If a public
key is built into the firmware (STELLA_UPDATE_PUBLIC_KEY), the manifest must
also carry a signature over SHA-256("stella-update-v1|<version>|<hex sha256
of the image>") that verifies with that key: then even a compromised update
host cannot install anything. See docs/ARCHITECTURE.md (Updates).

Signing needs:  pip install cryptography   (the plain `manifest` command does not)
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path


def crypto():
    """The cryptography package, imported only by the commands that sign or verify."""
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature
    except ImportError:  # pragma: no cover
        sys.exit("This command needs the 'cryptography' package: pip install cryptography")
    return dict(InvalidSignature=InvalidSignature, hashes=hashes, serialization=serialization, ec=ec,
                decode=decode_dss_signature, encode=encode_dss_signature)


PREFIX = "stella-update-v1|"


def image_digest(image: bytes) -> str:
    return hashlib.sha256(image).hexdigest()


def signed_message(version: str, image_hex: str) -> bytes:
    return f"{PREFIX}{version}|{image_hex}".encode()


def signed_digest(version: str, image_hex: str) -> str:
    return hashlib.sha256(signed_message(version, image_hex)).hexdigest()


def check_url(url):
    if not url.startswith("https://"):
        sys.exit("the image URL must be https://")
    if len(url) > 95:
        sys.exit("the image URL must be at most 95 characters (the device's JSON string limit)")


def write_manifest(args, image, extra=None):
    digest = image_digest(image)
    manifest = {"version": args.version, "url": args.url, "size": len(image), "sha256": digest}
    manifest.update(extra(digest) if extra else {})
    Path(args.out).write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {args.out}: stella {args.version}, {len(image)} bytes, sha256 {digest[:16]}...")


def cmd_manifest(args):
    check_url(args.url)
    write_manifest(args, Path(args.image).read_bytes())


def cmd_keygen(args):
    c = crypto()
    ec, serialization = c["ec"], c["serialization"]
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
    c = crypto()
    ec, hashes, serialization = c["ec"], c["hashes"], c["serialization"]
    key = serialization.load_pem_private_key(Path(args.key).read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        sys.exit("the key must be an ECDSA P-256 private key")
    check_url(args.url)

    def signature(digest):
        r, s = c["decode"](key.sign(signed_message(args.version, digest), ec.ECDSA(hashes.SHA256())))
        return {"sig_r": f"{r:064x}", "sig_s": f"{s:064x}"}

    write_manifest(args, Path(args.image).read_bytes(), signature)


def cmd_verify(args):
    manifest = json.loads(Path(args.manifest).read_text())
    image = Path(args.image).read_bytes()
    problems = []
    if manifest["size"] != len(image):
        problems.append("size differs from the image")
    if manifest["sha256"] != image_digest(image):
        problems.append("sha256 differs from the image")
    if "sig_r" in manifest and "sig_s" in manifest:
        c = crypto()
        pub = c["serialization"].load_pem_public_key(Path(args.public).read_bytes())
        signature = c["encode"](int(manifest["sig_r"], 16), int(manifest["sig_s"], 16))
        try:
            pub.verify(signature, signed_message(manifest["version"], manifest["sha256"]), c["ec"].ECDSA(c["hashes"].SHA256()))
        except c["InvalidSignature"]:
            problems.append("the signature does not verify")
    else:
        problems.append("the manifest has no signature")
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
    p = sub.add_parser("manifest")
    p.add_argument("--image", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--url", required=True)
    p.add_argument("--out", default="manifest.json")
    p.set_defaults(fn=cmd_manifest)
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
