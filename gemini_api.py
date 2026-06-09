import sys
import urllib.request
import urllib.error
import json
import os

API_KEY_PLACEHOLDER = "YOUR_GEMINI_API_KEY_HERE"
ENV_FILE = ".env"
DEFAULT_MODEL = "gemini-2.5-flash"

def load_local_env():
    if not os.path.exists(ENV_FILE):
        return

    with open(ENV_FILE, "r", encoding="utf-8") as env_file:
        for line in env_file:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue

            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip().strip('"').strip("'")
            if key and key not in os.environ:
                os.environ[key] = value

def call_gemini(prompt):
    load_local_env()
    api_key = os.environ.get("GEMINI_API_KEY", API_KEY_PLACEHOLDER).strip()
    model = os.environ.get("GEMINI_MODEL", DEFAULT_MODEL).strip()
    if not api_key or api_key == API_KEY_PLACEHOLDER:
        print("Please set your Gemini API key with the GEMINI_API_KEY environment variable.")
        return
    if not model:
        print("Please set a Gemini model with the GEMINI_MODEL environment variable.")
        return

    url = f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent?key={api_key}"
    data = {
        "contents": [{"parts": [{"text": prompt}]}]
    }
    
    req = urllib.request.Request(url, data=json.dumps(data).encode('utf-8'), headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req) as response:
            result = json.loads(response.read().decode('utf-8'))
            print(result['candidates'][0]['content']['parts'][0]['text'])
    except urllib.error.HTTPError as e:
        body = e.read().decode('utf-8', errors='replace')
        try:
            error = json.loads(body).get("error", {})
            message = error.get("message", body)
        except json.JSONDecodeError:
            message = body
        print(f"Error calling Gemini: HTTP {e.code} {e.reason}: {message}")
    except Exception as e:
        print(f"Error calling Gemini: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        call_gemini(" ".join(sys.argv[1:]))
    else:
        print("Usage: python gemini_api.py \"Your prompt here\"")
