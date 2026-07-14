import sys
import urllib.request
import urllib.error
import json
import os

DEFAULT_MODEL = "grok-3-mini"

def load_local_env():
    """Load environment variables from .env file in the same directory."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    env_file_path = os.path.join(script_dir, ".env")
    if not os.path.exists(env_file_path):
        return

    with open(env_file_path, "r", encoding="utf-8") as env_file:
        for line in env_file:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip().strip('"').strip("'")
            if key and key not in os.environ:
                os.environ[key] = value

def get_api_keys():
    """Collect all AI_API_KEY values from environment variables."""
    keys = []
    # Check numbered keys: AI_API_KEY_1, AI_API_KEY_2, AI_API_KEY_3, ...
    for i in range(1, 10):
        key = os.environ.get(f"AI_API_KEY_{i}", "").strip()
        if key:
            keys.append(key)
    # Also check a single AI_API_KEY as fallback
    single = os.environ.get("AI_API_KEY", "").strip()
    if single and single not in keys:
        keys.append(single)
    return keys

def call_ai(prompt):
    load_local_env()
    api_keys = get_api_keys()
    model = os.environ.get("AI_MODEL", DEFAULT_MODEL).strip()

    if not api_keys:
        print("Error: No API keys found. Please set AI_API_KEY_1, AI_API_KEY_2, etc. in your .env file.")
        return

    system_prompt = (
        "You are an AI assistant integrated into a custom C-based Virtual OS terminal. "
        "The OS supports exactly these commands: pwd, ls, cd, touch, mkdir, rm, rmdir, cat, write, append, cp, mv, echo, clear. "
        "The user will give you a request. If the request is to perform file operations (create, delete, move, etc.), "
        "you MUST output ONLY the exact terminal command(s) needed, nothing else. No markdown, no explanations, no chatty responses. "
        "For example, if the user says 'create a file name good.py', output exactly 'touch good.py'. "
        "If the user says 'delete it', output 'rm good.py'. "
        "If they just say 'hello', say 'Hello!'"
    )

    url = "https://api.groq.com/openai/v1/chat/completions"
    data = {
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": prompt}
        ],
        "stream": False
    }

    last_error_message = "No valid API keys available."

    for i, api_key in enumerate(api_keys):
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)"
        }

        req = urllib.request.Request(url, data=json.dumps(data).encode('utf-8'), headers=headers)
        try:
            with urllib.request.urlopen(req) as response:
                result = json.loads(response.read().decode('utf-8'))
                print(result['choices'][0]['message']['content'])
                return  # Success!
        except urllib.error.HTTPError as e:
            body = e.read().decode('utf-8', errors='replace')
            try:
                parsed = json.loads(body)
                error_val = parsed.get("error", body)
                if isinstance(error_val, dict):
                    message = error_val.get("message", body)
                else:
                    message = str(error_val)
            except json.JSONDecodeError:
                message = body

            last_error_message = f"HTTP {e.code}: {message}"

            # 403 Permission Denied, 429 Rate Limit, 500/502/503 Server errors -> try next key
            if e.code in [403, 429, 500, 502, 503]:
                continue
            else:
                break
        except Exception as e:
            last_error_message = str(e)
            continue

    print(f"Error calling AI: {last_error_message}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        call_ai(" ".join(sys.argv[1:]))
    else:
        print("Usage: python ai_api.py \"Your prompt here\"")
