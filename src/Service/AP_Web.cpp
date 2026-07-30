/*
    AP Web Dashboard — 为AP模式提供网页控制面板
    功能：实时日志窗口、控制按钮、IMU数据绘制
*/
#include "AP_Web.h"
#include "Config.h"
#include "Service/Console.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// ==================== 内嵌网页 ====================
// 完整的单页应用（HTML + CSS + JS），使用原始字符串字面量嵌入
static const char WEB_PAGE[] = R"raw(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Muggles' Wand</title>
<style>
:root{--bg:#1a1a2e;--panel:#16213e;--accent:#0f3460;--text:#e0e0e0;--green:#4caf84;--red:#e0556a;--warn:#e47352;--blue:#5b9bd5;--orange:#e8913a;--btn-bg:#1e3a5f;--btn-hover:#2a4a7f;--border:#2a3a5e}
*{box-sizing:border-box;margin:0;padding:0}
body{font:14px/1.5 'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;display:flex;flex-direction:column}
header{background:var(--panel);padding:10px 16px;border-bottom:1px solid var(--border);display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px}
header h1{font-size:18px;font-weight:600}
.status-dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}
.status-dot.on{background:var(--green);box-shadow:0 0 6px var(--green)}
.status-dot.off{background:var(--red)}
main{flex:1;display:flex;flex-direction:column;padding:12px;gap:12px;max-width:1200px;margin:0 auto;width:100%}
.panel{background:var(--panel);border-radius:8px;border:1px solid var(--border);overflow:hidden}
.panel-head{background:var(--accent);padding:6px 14px;font-size:13px;font-weight:600;display:flex;align-items:center;justify-content:space-between}
.panel-head button{background:none;border:1px solid var(--border);color:var(--text);padding:2px 10px;border-radius:4px;cursor:pointer;font-size:12px}
.panel-head button:hover{background:var(--btn-hover)}
#log-window{height:200px;overflow-y:auto;padding:8px 14px;font-family:'Cascadia Code','Fira Code',monospace;font-size:12px;line-height:1.6;white-space:pre-wrap;word-break:break-all;background:#0d0d1a}
#log-window::-webkit-scrollbar{width:6px}
#log-window::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}
.btn-row{display:flex;gap:8px;padding:6px 12px}
.btn-row:first-child{padding-top:12px}
.btn-row:last-child{padding-bottom:12px}
.btn-row button{flex:1;background:var(--btn-bg);color:var(--text);border:1px solid var(--border);padding:10px 8px;border-radius:6px;cursor:pointer;font-size:13px;transition:background .15s;text-align:center;min-height:44px;min-width:0}
.btn-row button:hover{background:var(--btn-hover)}
.btn-row button:active{background:var(--accent)}
.btn-row button.accent{background:var(--orange);border-color:var(--orange);color:#fff}
.btn-row button.danger{background:var(--red);border-color:var(--red);color:#fff}
.btn-row button.warning{background:var(--warn);border-color:var(--warn);color:#fff}
.btn-row input{flex:1;background:var(--bg);border:1px solid var(--border);color:var(--text);padding:8px 12px;border-radius:6px;font-size:13px;min-width:0}
.btn-row button.narrow,.btn-row input.narrow{flex:0.45;min-width:48px}
@media(max-width:768px){.btn-row{flex-wrap:wrap}}
.charts-area{display:flex;flex-direction:column;gap:12px}
.chart-panel{background:var(--panel);border-radius:8px;border:1px solid var(--border);overflow:hidden}
.chart-panel .panel-head{font-size:12px}
.chart-panel canvas{display:block;width:100%;height:220px;background:#0a0a16}
@media(max-width:768px){.chart-panel canvas{height:180px}}
</style>
</head>
<body>
<header>
  <h1>Muggles' Wand</h1>
  <span><span class="status-dot on" id="ws-dot"></span><span id="ws-status">已连接</span></span>
</header>
<main>
  <div class="panel">
    <div class="panel-head"><span>日志窗口</span><button onclick="clearLog()">清空</button></div>
    <div id="log-window"></div>
  </div>
  <div class="panel">
    <div class="panel-head">控制面板</div>
    <div class="btn-row">
      <button onclick="sendCmd('help')">帮助</button>
      <button onclick="sendCmd('debug')">调试</button>
      <button onclick="sendCmd('info')">信息</button>
      <button onclick="sendCmd('reboot')" class="warning">重启</button>
      <button onclick="sendCmd('sleep')" class="warning">休眠</button>
      <button onclick="sendCmd('shutdown')" class="danger">关机</button>
    </div>
    <div class="btn-row">
      <button onclick="sendCmd('ota')" class="accent">OTA</button>
      <button onclick="sendCmd('ap')" class="accent">AP</button>
      <button onclick="sendCmd('espnow')">ESP-NOW</button>
      <button onclick="sendCmd('ble')">BLE</button>
    </div>
    <div class="btn-row">
      <button onclick="sendCmd('inference')">IMU推理</button>
      <button onclick="sendCmd('imu')">IMU数据</button>
      <button onclick="sendCmd('stop')">IMU停止</button>
      <input type="number" id="gesture-id" placeholder="手势 ID" min="0" max="5" class="narrow">
      <button onclick="sendGesture()" class="narrow">发送</button>
    </div>
  </div>
  <div class="charts-area">
    <div class="chart-panel">
      <div class="panel-head">IMU 角速度 — valid_gx / valid_gz (rad/s)</div>
      <canvas id="chart-gx-gz"></canvas>
    </div>
    <div class="chart-panel">
      <div class="panel-head">手势状态机 — current_sta</div>
      <canvas id="chart-sta"></canvas>
    </div>
  </div>
</main>
<script>
// ============ WebSocket ============
var ws,reconnectTimer;
var MAX_LOG=500;
function connect(){
  ws=new WebSocket('ws://'+location.host+'/ws');
  ws.onopen=function(){
    document.getElementById('ws-dot').className='status-dot on';
    document.getElementById('ws-status').textContent='已连接';
    if(reconnectTimer){clearInterval(reconnectTimer);reconnectTimer=null}
  };
  ws.onmessage=function(e){appendLog(e.data)};
  ws.onclose=function(){
    document.getElementById('ws-dot').className='status-dot off';
    document.getElementById('ws-status').textContent='断开,重连中...';
    if(!reconnectTimer)reconnectTimer=setInterval(connect,3000);
  };
  ws.onerror=function(){ws.close()};
}
function sendCmd(cmd){if(ws&&ws.readyState===WebSocket.OPEN)ws.send(cmd)}
function sendGesture(){
  var v=document.getElementById('gesture-id').value.trim();
  if(v!=='')sendCmd('gesture '+v);
}

// ============ 日志窗口 ============
var logEl=document.getElementById('log-window');
var logLines=[];
var pendingText='';
function appendLog(text){
  pendingText+=text;
  var lines=pendingText.split('\n');
  // 最后一段是不完整的行（没有 \n 结尾），保留到下次
  pendingText=lines.pop();
  for(var i=0;i<lines.length;i++){
    var l=lines[i].replace(/\r$/,'');
    if(!l)continue;
    logLines.push(l);
    if(logLines.length>MAX_LOG)logLines.shift();
    // 尝试解析 IMU CSV 数据: valid_gx,valid_gz,current_sta
    var m=l.match(/^(-?\d+\.?\d*),(-?\d+\.?\d*),(-?\d+)$/);
    if(m)addImuPoint(parseFloat(m[1]),parseFloat(m[2]),parseInt(m[3]));
  }
  logEl.textContent=logLines.join('\n');
  logEl.scrollTop=logEl.scrollHeight;
}
function clearLog(){logLines=[];pendingText='';logEl.textContent='';chartGxGz.clear();chartSta.clear()}

// ============ 轻量 Canvas 图表 ============
function Chart(canvasId,config){
  this.canvas=document.getElementById(canvasId);
  this.ctx=this.canvas.getContext('2d');
  this.series=config.series||[];
  this.maxPoints=config.maxPoints||200;
  this.data=[];
  this.yMin=config.yMin;
  this.yMax=config.yMax;
  this.autoScale=(this.yMin===undefined);
  this.expandOnOverflow=config.expandOnOverflow||false;
  this.gridLines=config.gridLines||5;
  this.gridStep=config.gridStep||0;
  this.padding={top:10,right:15,bottom:25,left:45};
}
Chart.prototype.add=function(values){
  this.data.push(values);
  if(this.data.length>this.maxPoints)this.data.shift();
  this.draw();
};
Chart.prototype.clear=function(){this.data=[];this.draw()};
Chart.prototype.draw=function(){
  var c=this.canvas,dpr=window.devicePixelRatio||1;
  var w=c.clientWidth,h=c.clientHeight;
  // 保持 canvas 像素与 CSS 尺寸一致
  if(c.width!==w*dpr||c.height!==h*dpr){c.width=w*dpr;c.height=h*dpr}
  var ctx=this.ctx;
  ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.clearRect(0,0,w,h);
  var pw=w-this.padding.left-this.padding.right;
  var ph=h-this.padding.top-this.padding.bottom;
  if(this.data.length<2)return;
  // 计算 Y 范围
  var ylo=this.yMin!==undefined?this.yMin:Infinity;
  var yhi=this.yMax!==undefined?this.yMax:-Infinity;
  if(this.autoScale||this.expandOnOverflow){
    for(var i=0;i<this.data.length;i++){
      for(var j=0;j<this.data[i].length;j++){
        var v=this.data[i][j];
        if(v<ylo)ylo=v;if(v>yhi)yhi=v;
      }
    }
    if(this.expandOnOverflow){
      // 数据未超界时保持默认范围
      if(ylo>this.yMin)ylo=this.yMin;
      if(yhi<this.yMax)yhi=this.yMax;
      // 以 0 为中心对称扩展，缩放时保持 0 位置不变
      var absMax=Math.max(Math.abs(ylo),Math.abs(yhi),Math.abs(this.yMin||0),Math.abs(this.yMax||0));
      ylo=-absMax;yhi=absMax;
    }
  }
  if(yhi===ylo){yhi=ylo+1;ylo-=1}
  var yr=yhi-ylo;
  // 网格
  ctx.strokeStyle='#1e2e4a';ctx.lineWidth=1;
  var gs=this.gridStep;
  if(gs>0){
    var gStart=Math.ceil(ylo/gs)*gs;
    for(var gv=gStart;gv<=yhi+gs*0.001;gv+=gs){
      var gy=this.padding.top+ph-((gv-ylo)/yr*ph);
      ctx.beginPath();ctx.moveTo(this.padding.left,gy);ctx.lineTo(w-this.padding.right,gy);ctx.stroke();
      ctx.fillStyle='#667';ctx.font='10px monospace';ctx.textAlign='right';
      ctx.fillText(gv.toFixed(2),this.padding.left-4,gy+3);
    }
  }else{
    var gridLines=this.gridLines;
    for(var g=0;g<=gridLines;g++){
      var gy=this.padding.top+(ph*g/gridLines);
      ctx.beginPath();ctx.moveTo(this.padding.left,gy);ctx.lineTo(w-this.padding.right,gy);ctx.stroke();
      var label=(yhi-(yr*g/gridLines)).toFixed(2);
      ctx.fillStyle='#667';ctx.font='10px monospace';ctx.textAlign='right';
      ctx.fillText(label,this.padding.left-4,gy+3);
    }
  }
  // X轴标签
  ctx.fillStyle='#667';ctx.font='10px monospace';ctx.textAlign='center';
  ctx.fillText('0',this.padding.left,h-2);
  ctx.fillText(this.data.length,this.padding.left+pw,h-2);
  // 数据线
  for(var s=0;s<this.series.length;s++){
    ctx.strokeStyle=this.series[s].color;
    ctx.lineWidth=1.5;
    ctx.beginPath();
    var first=true;
    for(var i=0;i<this.data.length;i++){
      var x=this.padding.left+(pw*i/(this.maxPoints-1));
      var y=this.padding.top+ph-((this.data[i][s]-ylo)/yr*ph);
      if(first){ctx.moveTo(x,y);first=false}else ctx.lineTo(x,y);
    }
    ctx.stroke();
    // 图例
    if(this.data.length>0){
      var lx=this.padding.left+6+s*80,ly=this.padding.top+14;
      ctx.fillStyle=this.series[s].color;
      ctx.fillRect(lx-5,ly-6,10,8);
      ctx.fillStyle='#aaa';ctx.font='11px sans-serif';ctx.textAlign='left';
      ctx.fillText(this.series[s].label,lx+8,ly+2);
    }
  }
};

// ============ 图表初始化 ============
var chartGxGz=new Chart('chart-gx-gz',{
  series:[{label:'valid_gx',color:'#e0556a'},{label:'valid_gz',color:'#5b9bd5'}],
  maxPoints:200,yMin:-10,yMax:10,expandOnOverflow:true,gridStep:2
});
var chartSta=new Chart('chart-sta',{
  series:[{label:'current_sta',color:'#4caf84'}],
  maxPoints:200,yMin:0,yMax:4,gridLines:4
});

// ============ 窗口大小变化时重绘（防止暂停刷新时拉伸变形）============
window.addEventListener('resize',function(){
  chartGxGz.draw();
  chartSta.draw();
});

// ============ IMU 数据收集 ============
function addImuPoint(gx,gz,sta){
  chartGxGz.add([gx,gz]);
  chartSta.add([sta]);
}

// ============ 启动 ============
connect();
</script>
</body>
</html>
)raw";

// ==================== Web 服务模块 ====================
// 使用静态全局对象，避免 new/delete 导致的生命周期问题（AsyncTCP 回调竞争）
static AsyncWebServer webServer(80);
static AsyncWebSocket webSocket("/ws");
static bool webRunning = false;
static bool webRoutesSet = false;

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                       AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        DualSerial.printf("[Web] Client #%u connected\n", client->id());
        break;

    case WS_EVT_DISCONNECT:
        DualSerial.printf("[Web] Client #%u disconnected\n", client->id());
        break;

    case WS_EVT_DATA:
    {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->index == 0 && info->len == len &&
            info->opcode == WS_TEXT)
        {
            // 确保 null 终止
            char *cmd = (char *)data;
            cmd[len] = '\0';
            // 复用控制台解析逻辑
            console_parse(cmd);
        }
        break;
    }

    case WS_EVT_PONG:
    case WS_EVT_ERROR:
        break;
    }
}

void ap_web_start(void)
{
    if (webRunning)
        return;

    if (!webRoutesSet)
    {
        webSocket.onEvent(onWsEvent);
        webServer.addHandler(&webSocket);

        // 根路径：返回内嵌网页
        webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                      { request->send(200, "text/html; charset=utf-8", WEB_PAGE); });

        webRoutesSet = true;
    }

    webServer.begin();
    webRunning = true;
    DualSerial.println("[Web] HTTP + WebSocket server started on port 80");
}

void ap_web_stop(void)
{
    if (!webRunning)
        return;

    webSocket.closeAll();
    webServer.end();
    webRunning = false;
    DualSerial.println("[Web] Server stopped");
}

void ap_web_print(const char *buffer, size_t length)
{
    if (!webRunning)
        return;

    // WebSocket textAll 内部通过 AsyncTCP 排队发送，跨任务安全
    webSocket.textAll(buffer, length);
}
