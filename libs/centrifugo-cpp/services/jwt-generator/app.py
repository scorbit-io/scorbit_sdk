import os
import jwt
import time
from flask import Flask, request

app = Flask(__name__)
SECRET = os.getenv("JWT_SECRET", "my-secret-key")


@app.route("/")
def index():
    return "<h2>JWT Token Generator</h2><p>Usage: /token/username?seconds=300</p><p>Includes server-side subscriptions for channels: testchan, otherchan</p>"


@app.route("/token/<user>")
def get_token(user):
    seconds = int(request.args.get("seconds", 3600))  # Default to 1 hour
    exp_time = int(time.time()) + seconds

    channels = ["testchan", "otherchan"]
    token = jwt.encode(
        {"sub": user, "exp": exp_time, "channels": channels}, SECRET, algorithm="HS256"
    )
    return token


if __name__ == "__main__":
    print("JWT Generator running on http://0.0.0.0:3001")
    print("Usage: http://localhost:3001/token/test-user?seconds=300")
    app.run(host="0.0.0.0", port=3001)
