import urllib.request, urllib.parse, re
data = urllib.parse.urlencode({'q': 'hello'}).encode('utf-8')
req = urllib.request.Request('https://lite.duckduckgo.com/lite/', data=data, headers={'User-Agent': 'Mozilla/5.0'})
html = urllib.request.urlopen(req).read().decode('utf-8', errors='ignore')
html = re.sub(r'<[^>]+>', '', html)
print(html[:1000])
