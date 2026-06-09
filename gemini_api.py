import sys
import urllib.request
import json
import os

def call_gemini(prompt):
    api_key = os.environ.get("GEMINI_API_KEY", "YOUR_GEMINI_API_KEY_HERE")
    if api_key == "YOUR_GEMINI_API_KEY_HERE":
        print("Please set your Gemini API Key in gemini_api.py or via GEMINI_API_KEY environment variable.")
        return

    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash-latest:generateContent?key={api_key}"
    data = {
        "contents": [{"parts": [{"text": prompt}]}]
    }
    
    req = urllib.request.Request(url, data=json.dumps(data).encode('utf-8'), headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req) as response:
            result = json.loads(response.read().decode('utf-8'))
            print(result['candidates'][0]['content']['parts'][0]['text'])
    except Exception as e:
        print(f"Error calling Gemini: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        call_gemini(sys.argv[1])
    else:
        print("Usage: python gemini_api.py \"Your prompt here\"")
