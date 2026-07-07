"""
VirtualOS Web Fetcher — Fetches web pages and converts to plain text.
Called by the C GTK4 browser. Outputs JSON to stdout.
Usage: python web_fetch.py <url>
"""
import sys
import json
import urllib.request
import urllib.error
import html
import re
import ssl
from urllib.parse import urlparse, parse_qs, unquote

def strip_html(raw_html, base_url):
    """Convert HTML to readable plain text, filtering out JS, CSS, and formatting metadata."""
    # Remove HTML comments
    text = re.sub(r'<!--.*?-->', '', raw_html, flags=re.DOTALL)
    
    # Remove script and style blocks
    text = re.sub(r'<script[^>]*>.*?</script>', '', text, flags=re.DOTALL | re.IGNORECASE)
    text = re.sub(r'<style[^>]*>.*?</style>', '', text, flags=re.DOTALL | re.IGNORECASE)
    text = re.sub(r'<noscript[^>]*>.*?</noscript>', '', text, flags=re.DOTALL | re.IGNORECASE)
    
    # Extract title
    title_match = re.search(r'<title[^>]*>(.*?)</title>', raw_html, re.DOTALL | re.IGNORECASE)
    title = html.unescape(title_match.group(1).strip()) if title_match else ""
    
    # Resolve base domain
    try:
        parsed_base = urlparse(base_url)
        base_domain = f"{parsed_base.scheme}://{parsed_base.netloc}"
    except Exception:
        base_domain = "https://search.yahoo.com"

    # Extract links for reference
    links = []
    for m in re.finditer(r'<a\s+[^>]*href=["\']([^"\']+)["\'][^>]*>(.*?)</a>', text, re.DOTALL | re.IGNORECASE):
        href = m.group(1).strip()
        link_text = re.sub(r'<[^>]+>', '', m.group(2)).strip()
        if link_text and href and not href.startswith('#') and not href.startswith('javascript:'):
            # Resolve relative links
            if href.startswith('//'):
                href = "https:" + href
            elif href.startswith('/'):
                href = base_domain + href
            
            # Extract target URL from Yahoo redirect link
            if "/RU=" in href:
                try:
                    ru_part = href.split('/RU=')[1].split('/')[0]
                    href = unquote(ru_part)
                except Exception:
                    pass
                    
            links.append({"text": link_text[:80], "url": href})
    
    # Replace common block elements with newlines
    text = re.sub(r'<br\s*/?>', '\n', text, flags=re.IGNORECASE)
    text = re.sub(r'</(p|div|h[1-6]|li|tr|blockquote|section|article|header|footer|nav|main)>', '\n', text, flags=re.IGNORECASE)
    text = re.sub(r'<(p|div|h[1-6]|li|tr|blockquote|section|article|header|footer|nav|main)[^>]*>', '\n', text, flags=re.IGNORECASE)
    text = re.sub(r'<hr[^>]*>', '\n' + '─' * 60 + '\n', text, flags=re.IGNORECASE)
    
    # Bold/heading markers
    text = re.sub(r'<h1[^>]*>(.*?)</h1>', r'\n══ \1 ══\n', text, flags=re.DOTALL | re.IGNORECASE)
    text = re.sub(r'<h2[^>]*>(.*?)</h2>', r'\n── \1 ──\n', text, flags=re.DOTALL | re.IGNORECASE)
    text = re.sub(r'<h[3-6][^>]*>(.*?)</h[3-6]>', r'\n▸ \1\n', text, flags=re.DOTALL | re.IGNORECASE)
    text = re.sub(r'<b[^>]*>(.*?)</b>', r'*\1*', text, flags=re.DOTALL | re.IGNORECASE)
    text = re.sub(r'<strong[^>]*>(.*?)</strong>', r'*\1*', text, flags=re.DOTALL | re.IGNORECASE)
    text = re.sub(r'<em[^>]*>(.*?)</em>', r'_\1_', text, flags=re.DOTALL | re.IGNORECASE)
    text = re.sub(r'<i[^>]*>(.*?)</i>', r'_\1_', text, flags=re.DOTALL | re.IGNORECASE)
    
    # List items
    text = re.sub(r'<li[^>]*>', '  • ', text, flags=re.IGNORECASE)
    
    # Remove remaining HTML tags
    text = re.sub(r'<[^>]+>', '', text)
    
    # Decode HTML entities
    text = html.unescape(text)
    
    # Filter lines (remove JS boilerplate, CSS declarations, excessive code-like text)
    clean_lines = []
    for line in text.split('\n'):
        line_strip = line.strip()
        if not line_strip:
            clean_lines.append("")
            continue
            
        # Ignore lines that contain typical JavaScript syntax or CSS formatting
        if any(x in line_strip for x in ["{", "}", "function(", "var ", "const ", "let ", "Object.define", "prototype.", "margin:", "padding:", "color:", "background-"]):
            continue
            
        # Ignore lines with weird punctuation densities
        if len(line_strip) > 40 and (line_strip.count(';') + line_strip.count('(') + line_strip.count(')')) / len(line_strip) > 0.15:
            continue
            
        clean_lines.append(line)
        
    text = '\n'.join(clean_lines)
    
    # Clean up whitespace
    text = re.sub(r'[ \t]+', ' ', text)       # collapse horizontal spaces
    text = re.sub(r'\n\s*\n\s*\n+', '\n\n', text)  # max 2 consecutive newlines
    text = text.strip()
    
    # Limit unique links to 20
    seen = set()
    unique_links = []
    for link in links:
        if link["url"] not in seen and len(unique_links) < 20:
            # Clean up link text
            link_text = re.sub(r'\s+', ' ', link["text"]).strip()
            if link_text and not any(x in link["url"] for x in ["yahoo.com/preferences", "yahoo.com/search?", "help.yahoo.com"]):
                seen.add(link["url"])
                unique_links.append({"text": link_text, "url": link["url"]})
    
    return title, text, unique_links


import urllib.parse

def fetch(url):
    """Fetch a URL and return JSON result."""
    url = url.strip()
    
    # If it looks like a search query rather than a URL, redirect to Yahoo Search
    if " " in url or "." not in url:
        query = urllib.parse.quote_plus(url)
        url = f"https://search.yahoo.com/search?p={query}"
        
    if not url.startswith("http://") and not url.startswith("https://"):
        url = "https://" + url
    
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    
    req = urllib.request.Request(url, headers={
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        "Accept-Language": "en-US,en;q=0.5",
    })
    
    try:
        resp = urllib.request.urlopen(req, timeout=10, context=ctx)
        raw = resp.read()
        
        # Try to detect encoding
        charset = "utf-8"
        content_type = resp.headers.get("Content-Type", "")
        if "charset=" in content_type:
            charset = content_type.split("charset=")[-1].strip().split(";")[0]
        
        try:
            html_text = raw.decode(charset, errors="replace")
        except (LookupError, UnicodeDecodeError):
            html_text = raw.decode("utf-8", errors="replace")
        
        final_url = resp.geturl()
        title, text, links = strip_html(html_text, final_url)
        
        # Truncate text to avoid excessive output
        if len(text) > 30000:
            text = text[:30000] + "\n\n[Content truncated...]"
        
        return json.dumps({
            "ok": True,
            "url": final_url,
            "title": title,
            "text": text,
            "links": links
        }, ensure_ascii=True)
        
    except urllib.error.HTTPError as e:
        return json.dumps({
            "ok": False,
            "url": url,
            "error": f"HTTP {e.code}: {e.reason}"
        })
    except urllib.error.URLError as e:
        return json.dumps({
            "ok": False,
            "url": url,
            "error": f"Connection error: {str(e.reason)}"
        })
    except Exception as e:
        return json.dumps({
            "ok": False,
            "url": url,
            "error": str(e)
        })


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(json.dumps({"ok": False, "error": "No URL provided"}))
        sys.exit(1)
    
    result = fetch(sys.argv[1])
    print(result)
