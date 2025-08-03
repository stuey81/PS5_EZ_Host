#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>

#define HTTP_PORT 80
WebServer server(HTTP_PORT);
const char* AP_SSID = "PS5_EZ_HOST";

// Human‐readable file sizes
String formatBytes(size_t bytes) {
  if (bytes < 1024)                   return String(bytes) + " B";
  else if (bytes < (1024UL * 1024UL)) return String(bytes/1024.0,1) + " KB";
  else                                 return String(bytes/1024.0/1024.0,1) + " MB";
}

// 1) AppCache manifest
void handleManifest() {
  File f = SPIFFS.open("/cache.appcache","r");
  if (!f) return server.send(404,"text/plain","Manifest not found");
  server.sendHeader("Content-Type","text/cache-manifest");
  server.streamFile(f,"text/cache-manifest");
  f.close();
}

// 2) Exploit UI (root → index.html)
void handleIndex() {
  File f = SPIFFS.open("/index.html","r");
  if (!f) return server.send(500,"text/plain","Index not found");
  server.streamFile(f,"text/html");
  f.close();
}

// 3) Admin UI
void handleAdmin() {
  String html =
    "<!DOCTYPE html><html manifest=\"/cache.appcache\"><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>SPIFFS Payload Manager</title><style>"
      "body{font-family:Arial;margin:20px;background:#000;color:#fff;}"
      "table{width:100%;border-collapse:collapse;}"
      "th,td{padding:8px;border:1px solid #555;}"
      "th{background:#111;color:#fff;}"
      "a{color:#0af;text-decoration:none;}a:hover{text-decoration:underline;}"
    "</style></head><body>"
    "<h2>PS5 Payload Manager</h2>"
    "<form method='POST' action='/upload' enctype='multipart/form-data'>"
      "<input type='file' name='file' required> "
      "<input type='submit' value='Upload'>"
    "</form><hr>"
    "<table><tr><th>Filename</th><th>Size</th><th>Actions</th></tr>";

  File root = SPIFFS.open("/");
  File e = root.openNextFile();
  while (e) {
    if (!e.isDirectory()) {
      String n = e.name();
      bool show = (n == "index.html" || n == "payload_map.js");
      if (!show) {
        int d = n.lastIndexOf('.');
        if (d > 0) {
          String ex = n.substring(d);
          if (ex == ".elf" || ex == ".bin") show = true;
        }
      }
      if (show) {
        html += "<tr><td>" + n + "</td>"
             + "<td>" + formatBytes(e.size()) + "</td>"
             + "<td><a href='/download?file=" + n + "'>Download</a> | "
             + "<a href='/delete?file=" + n + "' onclick=\"return confirm('Delete " + n + "?')\">Delete</a></td></tr>";
      }
    }
    e = root.openNextFile();
  }

  html += "</table></body></html>";
  server.send(200,"text/html",html);
}

// 4) Upload handler
void handleUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    String fn = "/" + up.filename;
    SPIFFS.remove(fn);
    File f = SPIFFS.open(fn,"w"); f.close();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    File f = SPIFFS.open("/" + up.filename,"a");
    if (f) f.write(up.buf, up.currentSize);
    f.close();
  } else if (up.status == UPLOAD_FILE_END) {
    server.sendHeader("Location","/admin",true);
    server.send(303);
  }
}

// 5) Download handler
void handleDownload() {
  String want = server.arg("file");
  File root = SPIFFS.open("/");
  File e = root.openNextFile();
  while (e) {
    if (!e.isDirectory() && String(e.name()) == want) {
      File f = SPIFFS.open("/" + want,"r");
      server.sendHeader("Content-Disposition","attachment; filename=\"" + want + "\"");
      server.streamFile(f,"application/octet-stream");
      f.close();
      return;
    }
    e = root.openNextFile();
  }
  server.send(404,"text/plain","Not found: " + want);
}

// 6) Delete handler
void handleDelete() {
  if (!server.hasArg("file")) {
    server.send(400,"text/plain","No file specified");
    return;
  }
  String fn = "/" + server.arg("file");
  if (SPIFFS.exists(fn)) SPIFFS.remove(fn);
  server.sendHeader("Location","/admin",true);
  server.send(303);
}

void setup(){
  Serial.begin(115200);
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS Mount Failed");
    return;
  }

  WiFi.softAPConfig(IPAddress(10,1,1,1),
                    IPAddress(10,1,1,1),
                    IPAddress(255,255,255,0));
  WiFi.softAP(AP_SSID);
  Serial.printf("✅ AP \"%s\" @ %s\n",
                AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/cache.appcache", HTTP_GET,  handleManifest);
  server.on("/",             HTTP_GET,  handleIndex);
  server.on("/index.html",   HTTP_GET,  handleIndex);
  server.on("/admin",        HTTP_GET,  handleAdmin);
  server.on("/upload",       HTTP_POST, [](){ server.send(200); }, handleUpload);
  server.on("/download",     HTTP_GET,  handleDownload);
  server.on("/delete",       HTTP_GET,  handleDelete);

  server.serveStatic("/", SPIFFS, "/");
  server.onNotFound([](){
    server.send(404,"text/plain","Not found");
  });

  server.begin();
}

void loop(){
  server.handleClient();
}
