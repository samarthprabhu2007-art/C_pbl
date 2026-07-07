"""
VirtualOS Browser — Lightweight web browser using pywebview + WebView2.
Launches a native browser window with an address bar.
Usage: python browser.py [url]
"""
import sys
import os

try:
    import webview
except Exception as e:
    import traceback
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "browser_error.log"), "w") as f:
        f.write("Failed to import webview. Are you using MSYS2 Python instead of Windows Python?\n")
        f.write(traceback.format_exc())
    sys.exit(1)

DEFAULT_URL = "https://www.google.com"

def main():
    url = DEFAULT_URL
    if len(sys.argv) > 1:
        url = sys.argv[1]
        # Add https:// if user didn't type it
        if not url.startswith("http://") and not url.startswith("https://"):
            url = "https://" + url

    window = webview.create_window(
        "VirtualOS Browser",
        url,
        width=1100,
        height=750,
        text_select=True,
    )
    webview.start()

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        import traceback
        import os
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "browser_error.log"), "w") as f:
            f.write(traceback.format_exc())
        input("Press Enter to exit...")
