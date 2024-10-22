from ecdsa import SigningKey, NIST256p, VerifyingKey
import hashlib
import time

# Derive key from a seed (this could be stored securely)
seed = b"your_secure_seed_here"
sk = SigningKey.from_string(hashlib.sha256(seed).digest(), curve=NIST256p, hashfunc=hashlib.sha256)
vk = sk.get_verifying_key()

# Message to be signed (in this case, a nonce)
timestamp = str(int(time.time()))
uuid = "your_uuid_here"
nonce = bytes.fromhex(uuid) + timestamp.encode('utf-8')

# Sign the nonce
signature = sk.sign(nonce)

# Verify the signature
try:
    assert vk.verify(signature, nonce)
    print("Signature verified successfully!")
except:
    print("Signature verification failed!")

# Get the public key in hex format
public_key_hex = vk.to_string().hex()
print(f"Public Key: {public_key_hex}")

# Get the private key in hex format (be cautious with this in real applications)
private_key_hex = sk.to_string().hex()
print(f"Private Key: {private_key_hex}")

# Signature in hex format
signature_hex = signature.hex()
print(f"Signature: {signature_hex}")

# Example of reconstructing keys from hex (as might be done on the server side)
reconstructed_vk = VerifyingKey.from_string(bytes.fromhex(public_key_hex), curve=NIST256p)
reconstructed_sk = SigningKey.from_string(bytes.fromhex(private_key_hex), curve=NIST256p)

# Verify again with reconstructed key
assert reconstructed_vk.verify(signature, nonce)
print("Signature verified with reconstructed key!")