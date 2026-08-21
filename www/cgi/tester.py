#!/usr/bin/env python3
import os
import sys

def main():
    # 1. Output HTTP Headers
    # Using sys.stdout.write to ensure strict \r\n line endings
    sys.stdout.write("Content-Type: text/html\r\n")
    
    # Send a cookie back to the client to test header parsing
    sys.stdout.write("Set-Cookie: webserv_status=passed; Max-Age=3600; Path=/\r\n")
    
    # Blank line to separate headers from the body (\r\n\r\n)
    sys.stdout.write("\r\n")

    # 2. Gather Environment Variables
    method = os.environ.get("REQUEST_METHOD", "UNKNOWN")
    query_string = os.environ.get("QUERY_STRING", "")
    cookies = os.environ.get("HTTP_COOKIE", "No cookies received")
    
    # 3. Read POST body if applicable
    body = ""
    if method == "POST":
        try:
            content_length = int(os.environ.get("CONTENT_LENGTH", "0"))
            if content_length > 0:
                body = sys.stdin.read(content_length)
        except ValueError:
            body = "Error reading CONTENT_LENGTH"

    # 4. Generate HTML Output
    html = f"""
    <!DOCTYPE html>
    <html>
    <head>
        <title>Webserv CGI & Cookie Tester</title>
        <style>
            body {{ font-family: Arial, sans-serif; margin: 40px; background: #f4f4f9; }}
            .container {{ background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }}
            h2 {{ color: #333; border-bottom: 2px solid #ddd; padding-bottom: 5px; }}
            pre {{ background: #eee; padding: 10px; border-radius: 4px; overflow-x: auto; }}
            .success {{ color: green; font-weight: bold; }}
        </style>
    </head>
    <body>
        <div class="container">
            <h1>Webserv CGI Diagnostic</h1>
            
            <h2>1. Request Details</h2>
            <ul>
                <li><strong>Method:</strong> {method}</li>
                <li><strong>Query String:</strong> {query_string}</li>
            </ul>

            <h2>2. Cookie Test</h2>
            <p><strong>Received Cookies (HTTP_COOKIE):</strong></p>
            <pre>{cookies}</pre>
            <p class="success">Check your browser/curl to see if you received the 'webserv_status=passed' cookie!</p>

            <h2>3. POST Body Test</h2>
            <p><strong>Payload (stdin):</strong></p>
            <pre>{body if body else "No POST data received."}</pre>

            <h2>4. All Environment Variables</h2>
            <pre>"""
    
    for key, value in sorted(os.environ.items()):
        html += f"{key}: {value}\n"
        
    html += """</pre>
        </div>
    </body>
    </html>
    """
    
    sys.stdout.write(html)

if __name__ == "__main__":
    main()