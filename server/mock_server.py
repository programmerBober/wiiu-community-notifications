#!/usr/bin/env python3
"""
Simple HTTP mock server for Wii U Community Notifications feed.
Serves feed_example.json at http://<ip>:8080/feed.json
"""
import http.server
import socketserver
import os
import json

PORT = 8080
DIRECTORY = os.path.dirname(os.path.abspath(__file__))

class FeedHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def do_GET(self):
        if self.path in ('/', '/feed.json', '/api/feed.json'):
            feed_file = os.path.join(DIRECTORY, 'feed_example.json')
            if os.path.exists(feed_file):
                self.send_response(200)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.send_header('Access-Control-Allow-Origin', '*')
                self.end_headers()
                with open(feed_file, 'rb') as f:
                    self.wfile.write(f.read())
                return
        super().do_GET()

if __name__ == '__main__':
    with socketserver.TCPServer(("", PORT), FeedHandler) as httpd:
        print(f"[*] Wii U Community Notification Server listening on http://0.0.0.0:{PORT}/feed.json")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down server.")
