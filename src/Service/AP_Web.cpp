/*
    AP Web Dashboard — 为AP模式提供网页控制面板
    功能：交互式终端、控制按钮、IMU数据绘制
*/
#include "AP_Web.h"
#include "Config.h"
#include "Service/Console.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"
#include "HAL/HAL.h"
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
:root{--bg:#1a1a2e;--panel:#16213e;--accent:#0f3460;--text:#e0e0e0;--green:#4caf84;--red:#e0556a;--warn:#e47352;--blue:#5b9bd5;--orange:#e8913a;--purple:#b06fd1;--btn-bg:#1e3a5f;--btn-hover:#2a4a7f;--border:#2a3a5e;--mono:'Cascadia Code','Fira Code',Consolas,Menlo,Monaco,'DejaVu Sans Mono','Liberation Mono','Roboto Mono','Droid Sans Mono','Courier New','Microsoft YaHei','PingFang SC','Noto Sans CJK SC','Noto Sans SC',monospace}
*{box-sizing:border-box;margin:0;padding:0}
html{overflow-anchor:none}
body{font:14px/1.5 'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;display:flex;flex-direction:column}
header{background:var(--panel);padding:10px 16px;border-bottom:1px solid var(--border);display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px}
header h1{font-size:18px;font-weight:600}
.status-dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}
.status-dot.on{background:var(--green);box-shadow:0 0 6px var(--green)}
.status-dot.off{background:var(--red)}
.status-dot.warn{background:var(--orange)} /* 链路质量差：信号弱 */
/* 右上角系统状态：电量 / 电压 / 连接 */
.sys-info{display:inline-flex;align-items:center;gap:14px;flex-wrap:wrap}
.batt,.volt{display:inline-flex;align-items:center;gap:6px;font-variant-numeric:tabular-nums;transition:opacity .2s,filter .2s}
.ws-state{display:inline-flex;align-items:center}
.batt-icon{width:30px;height:14px;display:block}
/* 文字墨迹中心略低于行盒几何中心（数字无下伸部），图标下移半像素做视觉居中 */
.batt-icon,.volt-icon,.status-dot{position:relative;top:.5px}
.batt-shell{fill:none;stroke:currentColor;stroke-width:1.2;opacity:.75}
.batt-cap{fill:currentColor;opacity:.75}
.batt-fill{fill:var(--green);transition:width .3s,fill .3s}
.batt.mid .batt-fill{fill:var(--orange)}
.batt.low .batt-fill{fill:var(--red)}
/* 闪电用同色圆角描边勾出柔和轮廓，上下可略微超出电池外壳 */
.batt-bolt{fill:#fff;stroke:#fff;stroke-width:1.2;stroke-linejoin:round;stroke-linecap:round;filter:drop-shadow(0 .4px .5px rgba(0,0,0,.35));display:none}
.batt.charging .batt-bolt{display:block}
/* 断连时电量/电压置灰：数值保留，但当前不可信 */
.sys-info.stale .batt,.sys-info.stale .volt{opacity:.4;filter:grayscale(1)}
#batt-pct{min-width:4ch}
/* 电压图标：整块圆角矩形徽标，内嵌万用表直流电压符号 V⎓ */
.volt-icon{width:24px;height:16px;display:block}
.volt-icon .volt-bg{fill:#3c5f91} /* 比 --btn-bg 更亮，和电池图标的视觉权重对齐 */
.volt-icon path{fill:none;stroke:currentColor}
.volt-icon .volt-v{stroke-width:1.4;stroke-linecap:round;stroke-linejoin:round}
.volt-icon .volt-dc{stroke-width:1.2;stroke-linecap:butt}
#batt-volt{min-width:5ch}
main{flex:1;display:flex;flex-direction:column;padding:12px;gap:12px;max-width:1200px;margin:0 auto;width:100%}
.panel{background:var(--panel);border-radius:8px;border:1px solid var(--border);overflow:hidden}
/* 固定高度：有无右侧控件都不会把标题栏撑高 */
.panel-head{background:var(--accent);height:32px;padding:0 14px;font-size:13px;font-weight:600;display:flex;align-items:center;justify-content:space-between}
/* 右侧控件统一样式：清空按钮与折叠开关外观、边距一致，右边缘自然对齐 */
.panel-head button,.collapse-toggle{display:inline-flex;align-items:center;justify-content:center;gap:6px;background:none;border:1px solid var(--border);color:var(--text);padding:3px 10px;border-radius:4px;cursor:pointer;font-family:inherit;font-size:13px;line-height:1.2}
.panel-head button:hover,.collapse-toggle:hover{background:var(--btn-hover)}
.panel.collapsible>.panel-head{cursor:pointer;user-select:none}
.panel.collapsible>.panel-head:hover{background:var(--btn-hover)}
.collapse-toggle{font-weight:400}
.collapse-arrow{position:relative;display:inline-block;width:12px;height:12px;transition:transform .2s}
.collapse-arrow::before{content:'';position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);border-left:6px solid transparent;border-right:6px solid transparent;border-top:8px solid currentColor}
.collapse-text-collapsed{display:none}
.panel.collapsible.collapsed .collapse-arrow{transform:rotate(-90deg)}
.panel.collapsible.collapsed .collapse-text-expanded{display:none}
.panel.collapsible.collapsed .collapse-text-collapsed{display:inline}
.panel.collapsible.collapsed>.panel-body{display:none}
.terminal{background:#0d0d1a}
#log-window{height:420px;overflow-y:auto;padding:8px 14px;font-family:var(--mono);font-size:12px;line-height:1.6;white-space:pre-wrap;word-break:break-all;background:#0d0d1a}
#log-window::-webkit-scrollbar{width:6px}
#log-window::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}
.terminal-command{color:var(--green)}
.terminal-input-row{display:flex;align-items:center;gap:8px;padding:8px 12px;border-top:1px solid var(--border);background:#0a0a16;font-family:var(--mono)}
.terminal-prompt{color:var(--green);font-size:13px;font-weight:600}
.terminal-input-row input{flex:1;min-width:0;border:0;outline:0;background:transparent;color:var(--text);font-family:var(--mono);font-size:12px;line-height:1.6;caret-color:var(--green);padding:6px 0}
.terminal-input-row input::placeholder{color:#66708a}
.btn-row{display:flex;gap:8px;padding:6px 12px}
.btn-row:first-child{padding-top:12px}
.btn-row:last-child{padding-bottom:12px}
.btn-row button{display:flex;align-items:center;justify-content:center;flex:1;background:var(--btn-bg);color:var(--text);border:1px solid var(--border);padding:10px 8px;border-radius:6px;cursor:pointer;font-size:13px;transition:background .15s;text-align:center;min-height:44px;min-width:0}
.btn-row button:hover{background:var(--btn-hover)}
.btn-row button:active{background:var(--accent)}
.btn-row button.accent{background:var(--orange);border-color:var(--orange);color:#fff}
.btn-row button.danger{background:var(--red);border-color:var(--red);color:#fff}
.btn-row button.warning{background:var(--warn);border-color:var(--warn);color:#fff}
.btn-row button.green{background:var(--green);border-color:var(--green);color:#fff}
.btn-row button.blue{background:var(--blue);border-color:var(--blue);color:#fff}
.btn-row button.purple{background:var(--purple);border-color:var(--purple);color:#fff}
.btn-row input{flex:1;background:var(--bg);border:1px solid var(--border);color:var(--text);padding:8px 12px;border-radius:6px;font-size:13px;min-width:0}
/* 控制分组：手势 / POV */
.ctrl-group{display:flex;align-items:center;gap:4px;flex:1;min-width:0;background:var(--accent);border:1px solid var(--border);border-radius:6px;padding:2px 5px}
.ctrl-group input{flex:1 1 60px;min-width:36px;height:32px;padding:4px 10px}
.ctrl-group button{flex:1 1 auto;white-space:nowrap;height:32px;min-height:0;padding:0 12px}
/* 反向开关：方形滑块内嵌文字，随滑块上下滑动；空间紧张时优先让位（shrink:10） */
.switch{position:relative;display:inline-block;flex:0 10 56px;max-width:56px;min-width:26px}
.switch input{position:absolute;opacity:0;width:0;height:0;padding:0}
.rocker{display:block;position:relative;width:100%;height:32px;background:var(--btn-bg);border:1px solid var(--border);border-radius:6px;cursor:pointer;user-select:none;transition:background .2s,border-color .2s}
.rocker-dir{position:absolute;left:2px;right:2px;top:2px;height:16px;display:flex;align-items:center;justify-content:center;font-size:10px;line-height:1;color:var(--text);background:var(--btn-hover);border-radius:3px;transition:top .2s,background .2s,color .2s}
.switch input:checked+.rocker{background:var(--green);border-color:var(--green)}
.switch input:checked+.rocker .rocker-dir{top:12px;background:#fff;color:#1a1a2e}
@media(max-width:768px){.btn-row{flex-wrap:wrap}}
.charts-area{display:flex;flex-direction:column;gap:12px}
.chart-panel{background:var(--panel);border-radius:8px;border:1px solid var(--border);overflow:hidden}
.chart-panel .panel-head{font-size:12px}
.chart-panel canvas{display:block;width:100%;height:220px;background:#0a0a16}
@media(max-width:768px){#log-window{height:300px}.chart-panel canvas{height:180px}}
</style>
</head>
<body>
<header>
  <h1>Muggles' Wand</h1>
  <span class="sys-info stale">
    <span class="batt" id="batt" title="电量未知">
      <svg class="batt-icon" viewBox="0 0 30 14" aria-hidden="true">
        <rect class="batt-shell" x="1" y="1.5" width="24" height="11" rx="2.5"/>
        <rect class="batt-cap" x="26" y="4.5" width="2" height="5" rx="0.8"/>
        <rect class="batt-fill" id="batt-fill" x="3" y="3.5" width="0" height="7" rx="1.2"/>
        <path class="batt-bolt" d="M15 1 8.4 8.6h4.1l-1.4 4.2 6.5-7h-4.1z"/>
      </svg>
      <span id="batt-pct">--%</span>
    </span>
    <span class="volt" title="电池电压">
      <svg class="volt-icon" viewBox="0 0 24 16" aria-hidden="true">
        <rect class="volt-bg" x="0.5" y="0.5" width="23" height="15" rx="4"/>
        <path class="volt-v" d="M4.4 4.8 6.9 11.2 9.4 4.8"/>
        <path class="volt-dc" d="M11.4 6.5h8.4"/>
        <path class="volt-dc" d="M11.4 9.5h1.7M14.75 9.5h1.7M18.1 9.5h1.7"/>
      </svg>
      <span id="batt-volt">--V</span>
    </span>
    <span class="ws-state"><span class="status-dot off" id="ws-dot"></span><span id="ws-status">重连中...</span></span>
  </span>
</header>
<main>
  <div class="panel">
    <div class="panel-head"><span>交互式终端</span><button type="button" onclick="clearLog();terminalInput.focus()">清空 (Ctrl+L)</button></div>
    <div class="terminal">
      <div id="log-window"></div>
      <form class="terminal-input-row" id="terminal-form">
        <span class="terminal-prompt" aria-hidden="true">&gt;</span>
        <input id="terminal-input" type="text" maxlength="255" autocomplete="off" autocapitalize="off" spellcheck="false" enterkeyhint="send" aria-label="终端命令" placeholder="输入help查看命令列表，上下键查看历史命令">
      </form>
    </div>
  </div>
  <div class="panel collapsible">
    <div class="panel-head" role="button" tabindex="0" aria-expanded="true">
      <span>控制面板</span>
      <span class="collapse-toggle" aria-hidden="true"><span class="collapse-text-expanded">收起</span><span class="collapse-text-collapsed">展开</span><span class="collapse-arrow"></span></span>
    </div>
    <div class="panel-body">
      <div class="btn-row">
        <button onclick="sendCmd('help')">帮助</button>
        <button onclick="sendCmd('debug')">调试</button>
        <button onclick="sendCmd('info')">信息</button>
        <button onclick="sendCmd('reboot')" class="warning">重启</button>
        <button onclick="sendCmd('sleep')" class="warning">休眠</button>
        <button onclick="sendCmd('shutdown')" class="danger">关机</button>
      </div>
      <div class="btn-row">
        <button onclick="sendCmd('ota');setTimeout(function(){window.open('http://192.168.4.1/update','_blank')},500)" class="accent">OTA</button>
        <button onclick="sendCmd('ap')" class="accent">AP</button>
        <button onclick="sendCmd('espnow')" class="green">ESP-NOW</button>
        <button onclick="sendCmd('ble')" class="blue">BLE</button>
      </div>
      <div class="btn-row">
        <button onclick="sendCmd('inference')">IMU推理</button>
        <button onclick="sendCmd('imu')">IMU数据</button>
        <button onclick="sendCmd('stop')">IMU停止</button>
      </div>
      <div class="btn-row">
        <div class="ctrl-group">
          <input type="number" id="gesture-id" placeholder="手势 ID" min="0">
          <button onclick="sendGesture()" class="purple">触发手势</button>
        </div>
        <div class="ctrl-group">
          <input type="number" id="pov-id" placeholder="图案 ID" min="0">
          <label class="switch" title="reverse"><input type="checkbox" id="pov-rev"><span class="rocker"><span class="rocker-dir">反向</span></span></label>
          <button onclick="sendPov()" class="purple">POV</button>
        </div>
      </div>
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
var ws=null;
var reconnectTimer=null;
var connectingSince=0;
var lastSeen=0;
var lastPing=0;
var hiddenAt=document.hidden?Date.now():0;
var HEARTBEAT_MS=1000;          // 应用层心跳周期
var HEARTBEAT_TIMEOUT_MS=4000;  // 超过该时间没收到任何数据，判定链路已死
var CONNECT_TIMEOUT_MS=4000;    // WebSocket 握手超时
var RECONNECT_DELAY_MS=1000;    // 固定重连间隔，避免退避导致恢复快慢不定
var LINK_RTT_WINDOW=4;          // 取最近 N 次心跳 RTT 的平均值
var LINK_BAD_RTT_MS=150;        // 最近几次心跳平均 RTT 超过该值，判定信号弱
var LINK_BAD_PEAK_MS=300;       // 最近 2 次心跳中有单次 RTT 超过该值，判定信号弱
var LINK_BAD_SILENCE_MS=1800;   // 约 2 次心跳没收到 pong，判定信号弱
var MAX_LOG=500;
var MAX_COMMAND_BYTES=255;

// 链路质量状态
var pingSeq=0;
var rttSamples=[];
var lastPongAt=0;
var linkBad=false;
var linkBadStreak=0;
var linkGoodStreak=0;

function setWsStatus(state,text){
  document.getElementById('ws-dot').className='status-dot '+state;
  document.getElementById('ws-status').textContent=text;
  // 断开时电量/电压置灰，提示当前显示的是断连前的旧数据
  document.querySelector('.sys-info').classList.toggle('stale',state==='off');
}
function resetLinkQuality(){
  rttSamples=[];
  lastPongAt=0;
  linkBad=false;
  linkBadStreak=0;
  linkGoodStreak=0;
}
function sendHeartbeat(){
  if(!ws||ws.readyState!==WebSocket.OPEN)return;
  lastPing=Date.now();
  pingSeq++;
  // 带上序号和时间戳，服务端原样回显，用于计算 RTT
  try{ws.send('\x01PING:'+pingSeq+':'+lastPing)}catch(e){dropSocket()}
}
function updateBattery(soc,mv,chg){
  if(!isFinite(soc)||!isFinite(mv)||mv<=0)return;
  soc=Math.max(0,Math.min(100,soc));
  var pct=Math.round(soc);
  var batt=document.getElementById('batt');
  var cls='batt';
  if(chg)cls+=' charging';        // 充电时统一绿色 + 闪电
  else if(pct<10)cls+=' low';
  else if(pct<30)cls+=' mid';
  batt.className=cls;
  var volts=(mv/1000).toFixed(2)+'V';
  document.getElementById('batt-fill').setAttribute('width',(20*soc/100).toFixed(2));
  document.getElementById('batt-pct').textContent=pct+'%';
  document.getElementById('batt-volt').textContent=volts;
  batt.title='电量 '+pct+'%'+(chg?'，充电中':'')+'，'+volts;
}
function handleCtrlMessage(msg){
  if(msg.indexOf('\x01PONG:')!==0)return;
  var parts=msg.split(':');
  if(parts.length<3)return;
  // 服务端在 pong 末尾附带设备状态：:<电量%>:<电压mV>:<充电标志>
  if(parts.length>=6){
    updateBattery(parseFloat(parts[parts.length-3]),
                  parseInt(parts[parts.length-2],10),
                  parts[parts.length-1]==='1');
  }
  var ts=parseInt(parts[2],10);
  if(!ts)return;
  var rtt=Date.now()-ts;
  if(rtt<0||rtt>10000)return; // 丢弃异常样本
  lastPongAt=Date.now();
  rttSamples.push(rtt);
  if(rttSamples.length>LINK_RTT_WINDOW)rttSamples.shift();
  updateLinkQuality();
}
function updateLinkQuality(){
  if(!ws||ws.readyState!==WebSocket.OPEN)return;
  var now=Date.now();
  var sum=0;
  for(var i=0;i<rttSamples.length;i++)sum+=rttSamples[i];
  var avg=rttSamples.length?sum/rttSamples.length:0;
  var peak=0;
  for(var j=Math.max(0,rttSamples.length-2);j<rttSamples.length;j++){
    if(rttSamples[j]>peak)peak=rttSamples[j];
  }
  // 只看心跳：RTT 升高代表链路时延/重传，收不到 pong 代表丢包。
  // 不把"数据流中断"计入——用户主动 stop 或设备端停止输出并不代表链路差，
  // 而真实卡顿时 pong 同样会被堵住，上面两项已能覆盖。
  var bad=(rttSamples.length>=2&&(avg>LINK_BAD_RTT_MS||peak>LINK_BAD_PEAK_MS))||
          (lastPongAt&&now-lastPongAt>LINK_BAD_SILENCE_MS);
  if(bad){linkBadStreak++;linkGoodStreak=0}
  else{linkGoodStreak++;linkBadStreak=0}
  // 滞回：连续 2 次判定差才变黄，连续 3 次恢复正常才变绿，避免状态闪烁
  if(!linkBad&&linkBadStreak>=2){
    linkBad=true;
    setWsStatus('warn','信号弱');
  }else if(linkBad&&linkGoodStreak>=3){
    linkBad=false;
    setWsStatus('on','已连接');
  }
}
function scheduleReconnect(){
  if(reconnectTimer)return;
  reconnectTimer=setTimeout(function(){
    reconnectTimer=null;
    connect();
  },RECONNECT_DELAY_MS);
}
function retryNow(){
  // 网络/页面重新可用时立即尝试，不必等当前的重连定时器
  if(ws&&(ws.readyState===WebSocket.CONNECTING||ws.readyState===WebSocket.OPEN))return;
  if(reconnectTimer){clearTimeout(reconnectTimer);reconnectTimer=null}
  connect();
}
function dropSocket(){
  // 放弃当前 socket（可能卡在 CONNECTING/CLOSING 的半死状态）并安排重连。
  // 旧 socket 的事件由各回调里的 ws!==socket 判断统一忽略，不会再影响状态。
  var old=ws;
  ws=null;
  if(old){try{old.close()}catch(e){}}
  if(reconnectTimer){clearTimeout(reconnectTimer);reconnectTimer=null}
  resetLinkQuality();
  setWsStatus('off','重连中...');
  scheduleReconnect();
}
function connect(){
  // 单飞：同一时刻只允许存在一个连接/握手。旧实现用 setInterval 反复
  // 调用 connect()，而握手可能挂起十几秒，会叠加出大量半开 socket；
  // 网络恢复时它们同时完成握手，表现为一次性刷出很多 "Client # connected"。
  if(ws&&(ws.readyState===WebSocket.CONNECTING||ws.readyState===WebSocket.OPEN))return;
  if(ws){try{ws.close()}catch(e){}ws=null}
  setWsStatus('off','重连中...');
  var socket;
  try{
    socket=new WebSocket('ws://'+location.host+'/ws');
  }catch(e){
    scheduleReconnect();
    return;
  }
  ws=socket;
  connectingSince=Date.now();
  socket.onopen=function(){
    if(ws!==socket)return;
    resetLinkQuality();
    lastSeen=Date.now();
    lastPongAt=lastSeen;
    setWsStatus('on','已连接');
    sendHeartbeat();
  };
  socket.onmessage=function(e){
    if(ws!==socket)return;
    lastSeen=Date.now();
    // \x01 开头是服务端心跳等控制消息，只用于链路质量判断，不写入终端
    if(e.data.charCodeAt(0)===1){handleCtrlMessage(e.data);return}
    appendLog(e.data);
  };
  socket.onerror=function(){
    if(ws!==socket)return;
    dropSocket();
  };
  socket.onclose=function(){
    if(ws!==socket)return;
    ws=null;
    resetLinkQuality();
    setWsStatus('off','重连中...');
    scheduleReconnect();
  };
}

// 看门狗：主动发送心跳，并检测"僵尸连接"——
// 设备重启/AP 消失时 TCP 可能长时间不报错，页面会一直误显示"已连接"
setInterval(function(){
  if(document.hidden)return; // 后台标签页定时器会被浏览器节流，不能据此判断断线
  var now=Date.now();
  if(!ws){scheduleReconnect();return}
  if(ws.readyState===WebSocket.CONNECTING){
    if(now-connectingSince>CONNECT_TIMEOUT_MS)dropSocket();
  }else if(ws.readyState===WebSocket.OPEN){
    updateLinkQuality();
    if(now-lastSeen>HEARTBEAT_TIMEOUT_MS){
      dropSocket();
    }else if(now-lastPing>=HEARTBEAT_MS){
      sendHeartbeat();
    }
  }
},500);

// 从后台切回前台时立即检查/重连，不必等待被节流的定时器
document.addEventListener('visibilitychange',function(){
  if(document.hidden){hiddenAt=Date.now();return}
  var now=Date.now();
  var hiddenFor=hiddenAt?now-hiddenAt:0;
  hiddenAt=0;
  if(!ws||ws.readyState===WebSocket.CLOSED){
    retryNow();
  }else if(ws.readyState===WebSocket.OPEN){
    // 后台期间定时器被节流、应用层心跳停发，不能据此判断断线或链路质量；
    // 先重新开始一个存活窗口，再发心跳确认
    if(hiddenFor>HEARTBEAT_MS){
      lastSeen=now;
      lastPongAt=now;
      rttSamples=[];
    }
    if(now-lastSeen>HEARTBEAT_TIMEOUT_MS){
      dropSocket();
    }else{
      sendHeartbeat();
    }
  }
});

// 网络恢复 / 窗口重新获得焦点时立即重试，缩短连接恢复等待
window.addEventListener('online',retryNow);
window.addEventListener('focus',retryNow);
function sendCmd(cmd){
  cmd=String(cmd).trim();
  if(!cmd)return false;
  if(new TextEncoder().encode(cmd).length>MAX_COMMAND_BYTES){
    appendLog('[Web] 命令过长，最多 '+MAX_COMMAND_BYTES+' 字节。\n');
    return false;
  }
  if(!ws||ws.readyState!==WebSocket.OPEN){
    appendLog('[Web] 未连接，命令未发送。\n');
    return false;
  }
  ws.send(cmd);
  return true;
}
function sendGesture(){
  var v=document.getElementById('gesture-id').value.trim();
  if(v!=='')sendCmd('gesture '+v);
}
function sendPov(){
  var v=document.getElementById('pov-id').value.trim();
  if(v===''){sendCmd('pov');return} // 无参数: 打印图案清单与用法
  var rev=document.getElementById('pov-rev').checked?1:0;
  sendCmd('pov '+v+' '+rev);
}

// ============ 可折叠面板 ============
Array.prototype.forEach.call(document.querySelectorAll('.panel.collapsible>.panel-head'),function(head){
  function toggle(){
    var collapsed=head.parentElement.classList.toggle('collapsed');
    head.setAttribute('aria-expanded',collapsed?'false':'true');
  }
  head.addEventListener('click',toggle);
  head.addEventListener('keydown',function(event){
    if(event.key==='Enter'||event.key===' '){event.preventDefault();toggle()}
  });
});

// ============ 交互式终端 ============
var logEl=document.getElementById('log-window');
var terminalForm=document.getElementById('terminal-form');
var terminalInput=document.getElementById('terminal-input');
var pendingText='';
var followLog=true;
var cmdHistory=[];
var historyIndex=0;
var historyDraft='';

function clearLog(){
  pendingText='';
  logEl.replaceChildren();
  followLog=true;
  chartGxGz.clear();
  chartSta.clear();
}

function submitCommand(event){
  event.preventDefault();
  var cmd=terminalInput.value.trim();
  if(!cmd)return;
  if(sendCmd(cmd)){
    if(cmdHistory[cmdHistory.length-1]!==cmd)cmdHistory.push(cmd);
    if(cmdHistory.length>100)cmdHistory.shift();
    historyIndex=cmdHistory.length;
    historyDraft='';
    terminalInput.value='';
  }
  terminalInput.focus();
}

function recallHistory(direction){
  if(!cmdHistory.length)return;
  if(direction<0){
    if(historyIndex===cmdHistory.length)historyDraft=terminalInput.value;
    if(historyIndex>0)historyIndex--;
  }else if(historyIndex<cmdHistory.length){
    historyIndex++;
  }
  terminalInput.value=historyIndex===cmdHistory.length?historyDraft:cmdHistory[historyIndex];
  terminalInput.setSelectionRange(terminalInput.value.length,terminalInput.value.length);
}

terminalForm.addEventListener('submit',submitCommand);
terminalInput.addEventListener('keydown',function(event){
  if(event.key==='ArrowUp'){
    event.preventDefault();
    recallHistory(-1);
  }else if(event.key==='ArrowDown'){
    event.preventDefault();
    recallHistory(1);
  }
});
document.addEventListener('keydown',function(event){
  if((event.ctrlKey||event.metaKey)&&!event.altKey&&event.key.toLowerCase()==='l'){
    event.preventDefault();
    clearLog();
    terminalInput.focus();
  }
});
logEl.addEventListener('scroll',function(){
  followLog=logEl.scrollHeight-logEl.scrollTop-logEl.clientHeight<24;
});

function appendLog(text){
  pendingText+=text;
  var lines=pendingText.split('\n');
  // 最后一段是不完整的行（没有 \n 结尾），保留到下次
  pendingText=lines.pop();
  for(var i=0;i<lines.length;i++){
    var l=lines[i].replace(/\r$/,'');
    if(!l)continue;
    var row=document.createElement('div');
    row.textContent=l;
    if(l.indexOf('> ')===0)row.className='terminal-command';
    logEl.appendChild(row);
    if(logEl.childElementCount>MAX_LOG)logEl.firstElementChild.remove();
    // 尝试解析 IMU CSV 数据: valid_gx,valid_gz,current_sta
    var m=l.match(/^(-?\d+\.?\d*),(-?\d+\.?\d*),(-?\d+)$/);
    if(m)addImuPoint(parseFloat(m[1]),parseFloat(m[2]),parseInt(m[3]));
  }
  if(followLog)logEl.scrollTop=logEl.scrollHeight;
}

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

// 应用层心跳：0x01 开头的文本帧不进入终端日志，只用于判断链路质量
static const uint8_t WS_CTRL_PREFIX = 0x01;
static const char WS_CTRL_PING[] = "PING";
static const char WS_CTRL_PONG_PREFIX[] = "\x01PONG";

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    // 服务端定期 ping：客户端失联时尽快触发 ACK 超时并清理，
    // 避免浏览器换页/重启后残留大量僵尸连接
    client->keepAlivePeriod(2);
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
      // 心跳帧不应交给控制台解析
      if (len >= 1 && data[0] == WS_CTRL_PREFIX)
      {
        if (len >= 5 && memcmp(data + 1, WS_CTRL_PING, 4) == 0)
        {
          // 回显 ":<seq>:<ts>" 供浏览器计算 RTT，并追加设备状态
          // :<电量%>:<电压mV>:<是否充电>，前端据此更新右上角电量显示
          int soc = (int)(HAL::MAX17048_getSOC() + 0.5f);
          if (soc < 0)
            soc = 0;
          else if (soc > 100)
            soc = 100;
          int mv = (int)(HAL::MAX17048_getVoltage() * 1000.0f + 0.5f);

          char status[32];
          int statusLen = snprintf(status, sizeof(status), ":%d:%d:%d",
                                   soc, mv, HAL::power_getChargeStatus() ? 1 : 0);
          if (statusLen < 0 || statusLen >= (int)sizeof(status))
            statusLen = 0;

          const size_t prefixLen = sizeof(WS_CTRL_PONG_PREFIX) - 1;
          char pong[80];
          size_t bodyLen = len - 5;
          if (bodyLen > sizeof(pong) - prefixLen - (size_t)statusLen)
            bodyLen = sizeof(pong) - prefixLen - (size_t)statusLen;
          memcpy(pong, WS_CTRL_PONG_PREFIX, prefixLen);
          memcpy(pong + prefixLen, data + 5, bodyLen);
          memcpy(pong + prefixLen + bodyLen, status, (size_t)statusLen);
          client->text((const uint8_t *)pong, prefixLen + bodyLen + (size_t)statusLen);
        }
        break;
      }

      if (!console_submit((const char *)data, len))
      {
        client->text("[Web] Command rejected (empty, too long, or busy)\n");
      }
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
    // 禁止缓存，确保固件更新后浏览器拿到的是最新页面逻辑
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
                   AsyncWebServerResponse *response =
                       request->beginResponse(200, "text/html; charset=utf-8", WEB_PAGE);
                   response->addHeader("Cache-Control", "no-store");
                   request->send(response); });

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
  if (!webRunning || buffer == nullptr || length == 0)
    return;

  // 没有客户端时直接返回：textAll 会先分配共享缓冲区，
  // 日志按行高频输出时这部分开销完全没必要
  if (webSocket.count() == 0)
    return;

  // WebSocket textAll 内部通过 AsyncTCP 排队发送，跨任务安全
  webSocket.textAll(buffer, length);
}
