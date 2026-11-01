/*
    AP Web Dashboard — 为AP模式提供网页控制面板
    功能：交互式终端、控制按钮、IMU数据绘制
*/
#include "AP_Web.h"
#include "Config.h"
#include "Service/Console.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"
#include "Service/BLE.h"
#include "Service/AP.h"
#include "HAL/HAL.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <string.h>

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
:root{--bg:#1a1a2e;--panel:#16213e;--accent:#0f3460;--text:#e0e0e0;--green:#4caf84;--red:#e0556a;--warn:#e47352;--blue:#5b9bd5;--orange:#e8913a;--purple:#b06fd1;--violet:#8a5cd6;--teal:#2f9e95;--rose:#d97884;--btn-bg:#1e3a5f;--btn-hover:#2a4a7f;--border:#2a3a5e;--mono:'Cascadia Code','Fira Code',Consolas,Menlo,Monaco,'DejaVu Sans Mono','Liberation Mono','Roboto Mono','Droid Sans Mono','Courier New','Microsoft YaHei','PingFang SC','Noto Sans CJK SC','Noto Sans SC',monospace}
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
.batt-icon,.volt-icon,.ble-icon,.ap-icon,.wifi-icon,.espnow-icon,.status-dot{position:relative;top:.5px}
.batt-shell{fill:none;stroke:currentColor;stroke-width:1.2;opacity:.75}
.batt-cap{fill:currentColor;opacity:.75}
.batt-fill{fill:var(--green);transition:width .3s,fill .3s}
.batt.mid .batt-fill{fill:var(--orange)}
.batt.low .batt-fill{fill:var(--red)}
/* 闪电用同色圆角描边勾出柔和轮廓，上下可略微超出电池外壳 */
.batt-bolt{fill:#fff;stroke:#fff;stroke-width:1.2;stroke-linejoin:round;stroke-linecap:round;filter:drop-shadow(0 .4px .5px rgba(0,0,0,.35));display:none}
.batt.charging .batt-bolt{display:block}
/* 断连时状态置灰：数值保留，但当前不可信 */
.sys-info.stale .batt,.sys-info.stale .volt,.sys-info.stale .ble,.sys-info.stale .ap,.sys-info.stale .wifi,.sys-info.stale .espnow{opacity:.4;filter:grayscale(1)}
/* 电量/电压文本不预留固定宽度：多出的透明余量会把右侧间距撑大，导致图标疏密不均；
   电压恒为 5 字符且启用等宽数字，宽度本就稳定 */
/* 电压图标：整块圆角矩形徽标，内嵌万用表直流电压符号 V⎓ */
.volt-icon{width:24px;height:16px;display:block}
.volt-icon .volt-bg{fill:#3c5f91} /* 比 --btn-bg 更亮，和电池图标的视觉权重对齐 */
.volt-icon path{fill:none;stroke:currentColor}
.volt-icon .volt-v{stroke-width:1.4;stroke-linecap:round;stroke-linejoin:round}
.volt-icon .volt-dc{stroke-width:1.2;stroke-linecap:butt}
/* 无线状态图标：蓝牙三态（关闭/广播中/已连接）、AP / WiFi 开关与 ESP-NOW 开关 */
.ble,.ap,.wifi,.espnow{display:inline-flex;align-items:center;transition:opacity .2s,filter .2s}
.ble-icon{width:14px;height:20px;display:block}
.ap-icon,.wifi-icon,.espnow-icon{width:20px;height:20px;display:block}
.wireless-glyph{fill:none;stroke:currentColor;stroke-width:1.6;stroke-linecap:round;stroke-linejoin:round}
.ble-icon .wireless-glyph{stroke-width:1.8} /* 蓝牙 viewBox 16→14px 有缩放，1.8×0.875≈1.6px，与 ESP-NOW 渲染粗细一致 */
.espnow-icon .node,.ap-icon .node,.wifi-icon .node{fill:currentColor}
.bt-dot{fill:currentColor;display:none} /* 已连接时在两侧空缺处点亮 */
.ble.connected .bt-dot{display:block}
.ble.off,.ap.off,.wifi.off,.espnow.off{color:#66708a}
.ble.on,.wifi.on{color:var(--blue)} /* 已开启、等待主机连接 */
.ap.on,.ble.connected,.wifi.connected,.espnow.on{color:var(--green);filter:drop-shadow(0 0 4px rgba(76,175,132,.5))}
.ble.unknown,.ap.unknown,.wifi.unknown,.espnow.unknown{opacity:.35}
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
.btn-row button.violet{background:var(--violet);border-color:var(--violet);color:#fff}
.btn-row button.teal{background:var(--teal);border-color:var(--teal);color:#fff}
.btn-row button.rose{background:var(--rose);border-color:var(--rose);color:#fff}
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
/* 控制面板二级页面：手势绑定 / 参数配置 */
.ctrl-view{display:none}
.ctrl-view.active{display:block}
.subpage-bar{display:flex;align-items:center;gap:8px;padding:12px 12px 0}
.subpage-bar .subpage-title{flex:1;min-width:72px;font-size:13px;font-weight:600}
.subpage-bar button{background:var(--btn-bg);color:var(--text);border:1px solid var(--border);padding:9px 16px;border-radius:8px;cursor:pointer;font-family:inherit;font-size:13px;line-height:1.3;min-height:40px}
.subpage-bar button:hover{background:var(--btn-hover)}
/* 页面末尾的操作按钮：比顶栏按钮更大，便于触屏点按 */
.subpage-actions{display:flex;flex-wrap:wrap;gap:10px;padding:4px 12px 14px}
.subpage-actions button{background:var(--btn-bg);color:var(--text);border:1px solid var(--border);padding:10px 20px;border-radius:8px;cursor:pointer;font-family:inherit;font-size:13px;line-height:1.3;min-height:42px}
.subpage-actions button:hover{background:var(--btn-hover)}
.subpage-actions button.warning{background:var(--warn);border-color:var(--warn);color:#fff}
.subpage-actions button.green{background:var(--green);border-color:var(--green);color:#fff}
.subpage-note{padding:6px 12px 0;color:#8892aa;font-size:12px;line-height:1.5}
.subpage-status{padding:6px 12px 0;font-size:12px;line-height:1.5;min-height:18px;color:var(--green);white-space:pre-wrap;word-break:break-all}
.subpage-status.pending{color:var(--orange)}
.subpage-status.error{color:var(--red)}
.cfg-list{display:flex;flex-direction:column;gap:8px;padding:8px 12px 12px}
/* 每一行都是一张独立卡片，宽屏窄屏保持统一风格 */
.cfg-row{display:flex;align-items:center;gap:8px;flex-wrap:nowrap;padding:8px 10px;border:1px solid var(--border);border-radius:8px;background:rgba(255,255,255,.025)}
.cfg-row .cfg-label{flex:0 0 72px;font-size:12px;color:#aab3c9;white-space:nowrap}
.cfg-row select,.cfg-row input{flex:1 1 120px;min-width:90px;background:var(--bg);border:1px solid var(--border);color:var(--text);padding:7px 10px;border-radius:6px;font-size:12px;font-family:inherit}
.cfg-row input{flex-grow:1.3} /* 参数输入通常比 APP 名称更长，分到更多宽度 */
.cfg-row select:disabled,.cfg-row input:disabled{opacity:.45}
.cfg-row button{flex:0 0 auto;background:var(--btn-bg);color:var(--text);border:1px solid var(--border);padding:7px 14px;border-radius:6px;cursor:pointer;font-family:inherit;font-size:12px;line-height:1.4}
.cfg-row button:hover{background:var(--btn-hover)}
.cfg-row.saved button{background:var(--green);border-color:var(--green);color:#fff}
.cfg-row.failed button{background:var(--red);border-color:var(--red);color:#fff}
/* 窄屏：手势卡两行（手势/APP、参数），保存按钮占满右侧两行；
   配置卡保持单行，输入框吃掉剩余宽度 */
@media(max-width:520px){
  .cfg-row.map-row{
    display:grid;
    grid-template-columns:auto minmax(0,1fr) auto;
    grid-template-areas:"label select save" "input input save";
    gap:6px 8px;
  }
  .map-row .cfg-label{grid-area:label;flex-basis:auto;align-self:center}
  .map-row select{grid-area:select;min-width:0}
  .map-row input{grid-area:input;min-width:0}
  .map-row button{grid-area:save;align-self:stretch;padding:7px 10px}
  .cfg-row.config-row{
    display:grid;
    grid-template-columns:auto minmax(0,1fr) auto;
    grid-template-areas:"label input save";
    gap:6px;
  }
  .config-row .cfg-label{grid-area:label;flex-basis:auto}
  .config-row input{grid-area:input;min-width:0}
  .config-row button{grid-area:save;padding:7px 12px}
  .subpage-actions{gap:8px}
  .subpage-actions button{flex:1 1 auto;padding:10px 10px}
}
</style>
</head>
<body>
<header>
  <h1>Muggles' Wand</h1>
  <span class="sys-info stale">
    <span class="ble unknown" id="ble" title="蓝牙状态未知">
      <svg class="ble-icon" viewBox="0 0 16 22" aria-hidden="true">
        <path class="wireless-glyph" d="M3 6l10 10-5 5V1l5 5L3 16"/>
        <circle class="bt-dot" cx="1.7" cy="11" r="1.6"/>
        <circle class="bt-dot" cx="14.3" cy="11" r="1.6"/>
      </svg>
    </span>
    <span class="ap unknown" id="ap" title="AP 状态未知">
      <svg class="ap-icon" viewBox="0 0 20 20" aria-hidden="true">
        <circle class="node" cx="10" cy="7" r="1.8"/>
        <path class="wireless-glyph" d="M7.17 4.17A4 4 0 0 0 7.17 9.83"/>
        <path class="wireless-glyph" d="M12.83 4.17A4 4 0 0 1 12.83 9.83"/>
        <path class="wireless-glyph" d="M5.33 2.33A6.6 6.6 0 0 0 5.33 11.67"/>
        <path class="wireless-glyph" d="M14.67 2.33A6.6 6.6 0 0 1 14.67 11.67"/>
        <path class="wireless-glyph" d="M10 8.9v2.8M10 11.7 6.7 18.3M10 11.7l3.3 6.6M7.9 15.2h4.2"/>
      </svg>
    </span>
    <span class="wifi unknown" id="wifi" title="WiFi 状态未知">
      <svg class="wifi-icon" viewBox="0 0 20 20" aria-hidden="true">
        <circle class="node" cx="10" cy="17" r="1.7"/>
        <path class="wireless-glyph" d="M7 12.87A5.1 5.1 0 0 1 13 12.87"/>
        <path class="wireless-glyph" d="M4.3 9.15A9.7 9.7 0 0 1 15.7 9.15"/>
        <path class="wireless-glyph" d="M1.65 5.51A14.2 14.2 0 0 1 18.35 5.51"/>
      </svg>
    </span>
    <span class="espnow unknown" id="espnow" title="ESP-NOW 状态未知">
      <svg class="espnow-icon" viewBox="0 0 20 20" aria-hidden="true">
        <circle class="wireless-glyph" cx="10" cy="10" r="8.3"/>
        <circle class="node" cx="5.6" cy="14.4" r="1.45"/>
        <path class="wireless-glyph" d="M5.6 10.4A4 4 0 0 1 9.6 14.4M5.6 7.9A6.5 6.5 0 0 1 12.1 14.4"/>
      </svg>
    </span>
    <span class="batt" id="batt" title="电量未知">
      <svg class="batt-icon" viewBox="0 0 30 14" aria-hidden="true">
        <rect class="batt-shell" x="1" y="1.5" width="24" height="11" rx="2.5"/>
        <rect class="batt-cap" x="26" y="4.5" width="2" height="5" rx="0.8"/>
        <rect class="batt-fill" id="batt-fill" x="3" y="3.5" width="0" height="7" rx="1.2"/>
        <path class="batt-bolt" d="M15 1 8.4 8.6h4.1l-1.4 4.2 6.5-7h-4.1z"/>
      </svg>
      <span id="batt-pct">--%</span>
    </span>
    <span class="volt" id="volt" title="电压未知">
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
      <div id="ctrl-main" class="ctrl-view active">
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
          <button onclick="sendCmd('ble')" class="blue">BLE</button>
          <button onclick="sendCmd('ap')" class="green">AP</button>
          <button onclick="sendCmd('wifi')" class="green">WiFi</button>
          <button onclick="sendCmd('espnow')" class="green">ESP-NOW</button>
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
            <button onclick="sendPov()" class="rose">POV</button>
          </div>
        </div>
        <div class="btn-row">
          <button onclick="openCtrlPage('map')" class="violet">手势绑定 &gt;</button>
          <button onclick="openCtrlPage('config')" class="teal">参数配置 &gt;</button>
        </div>
      </div>
      <div id="ctrl-map" class="ctrl-view">
        <div class="subpage-bar">
          <button type="button" onclick="closeCtrlPage()">&lt; 返回</button>
          <span class="subpage-title">手势绑定</span>
          <button type="button" onclick="requestMapList()">刷新</button>
        </div>
        <div class="subpage-note">为每个手势配置对应的功能，保存后立即生效；也可用底部「全部保存」一次性提交所有修改。</div>
        <div class="subpage-status" id="map-status"></div>
        <div class="cfg-list" id="map-list"></div>
        <div class="subpage-actions">
          <button type="button" onclick="saveAllMap()" class="green">全部保存</button>
          <button type="button" onclick="mapReset()" class="warning">恢复默认值</button>
        </div>
      </div>
      <div id="ctrl-config" class="ctrl-view">
        <div class="subpage-bar">
          <button type="button" onclick="closeCtrlPage()">&lt; 返回</button>
          <span class="subpage-title">参数配置</span>
          <button type="button" onclick="requestConfigList()">刷新</button>
        </div>
        <div class="subpage-note">修改会写入 NVS，重启后生效；也可用底部「全部保存」一次性提交所有修改。</div>
        <div class="subpage-status" id="config-status"></div>
        <div class="cfg-list" id="config-list"></div>
        <div class="subpage-actions">
          <button type="button" onclick="saveAllConfig()" class="green">全部保存</button>
          <button type="button" onclick="configClear('wifi')">清除WiFi配置</button>
          <button type="button" onclick="configClear('mqtt')">清除MQTT配置</button>
          <button type="button" onclick="configReset()" class="warning">恢复默认值</button>
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
  // 电量与电压各自独立显示在悬停提示中
  batt.title='电量 '+pct+'%';
  document.getElementById('volt').title='电压 '+volts;
}
function updateWireless(ble,espnowOn,apOn,wifiState){
  var bt=document.getElementById('ble');
  if(isFinite(ble)){
    bt.className='ble '+(ble===2?'connected':ble===1?'on':'off');
    bt.title=ble===2?'蓝牙已连接':ble===1?'蓝牙已开启，未连接':'蓝牙已关闭';
  }
  if(apOn!==undefined){
    var ap=document.getElementById('ap');
    ap.className='ap '+(apOn?'on':'off');
    ap.title=apOn?'AP 已开启':'AP 已关闭';
  }
  if(isFinite(wifiState)){
    var wf=document.getElementById('wifi');
    wf.className='wifi '+(wifiState===2?'connected':wifiState===1?'on':'off');
    wf.title=wifiState===2?'WiFi 已连接':wifiState===1?'WiFi 已开启，未连接':'WiFi 已关闭';
  }
  var en=document.getElementById('espnow');
  en.className='espnow '+(espnowOn?'on':'off');
  en.title=espnowOn?'ESP-NOW 已开启':'ESP-NOW 已关闭';
}
function handleCtrlMessage(msg){
  if(msg.indexOf('\x01PONG:')!==0)return;
  var parts=msg.split(':');
  if(parts.length<3)return;
  // 服务端在 pong 末尾附带设备状态：
  // :<蓝牙0/1/2>:<ESP-NOW 0/1>:<电量%>:<电压mV>:<充电标志>:<AP 0/1>:<WiFi 0/1/2>
  // AP/WiFi 追加在末尾，旧页面只读前 8 段，忽略新增字段
  if(parts.length>=8){
    if(parts.length>=10){
      updateWireless(parseInt(parts[3],10),parts[4]==='1',
                     parseInt(parts[8],10)!==0,parseInt(parts[9],10));
    }else{
      updateWireless(parseInt(parts[3],10),parts[4]==='1');
    }
    updateBattery(parseFloat(parts[5]),
                  parseInt(parts[6],10),
                  parts[7]==='1');
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
function sendCmd(cmd,keepWhitespace){
  // config 的值是“行内剩余部分”，首尾空格也可能是值本身，raw 模式不做 trim
  cmd=keepWhitespace?String(cmd):String(cmd).trim();
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

// ============ 控制面板二级页面：手势绑定 / 参数配置 ============
// 页面本身不直接读写 NVS：显示内容来自 map list / config list 的终端回显，
// 每个保存按钮发送对应的 map / config 控制台命令，与手动输入命令完全等价。
var ctrlPage='main';

var MAP_APP_INFO=[
  {value:'',label:'— 未绑定 —',param:false},
  {value:'espnow',label:'espnow（ESP-NOW 信号）',param:true,placeholder:'信号值，如 0x02'},
  {value:'lumos',label:'lumos（荧光闪烁）',param:false},
  {value:'pov',label:'pov（光绘模式）',param:false},
  {value:'ble_keyboard',label:'ble_keyboard（BLE 键盘宏）',param:true,placeholder:'按键序列，如 [win+r],c,m,d,enter 或 [cmd+space] ; Mac 键名用 cmd/option，具体语法查看文档'},
  {value:'ble_mouse',label:'ble_mouse（BLE 鼠标模式）',param:false},
  {value:'ble_knob',label:'ble_knob（BLE 音量旋钮模式）',param:false},
  {value:'ha',label:'ha（Home Assistant）',param:true,placeholder:'事件名，如 turn_on ; 只允许小写字母、数字和下划线'},
];
var MAP_APP_LOOKUP={};
for(var mapAppIndex=0;mapAppIndex<MAP_APP_INFO.length;mapAppIndex++){
  MAP_APP_LOOKUP[MAP_APP_INFO[mapAppIndex].value]=MAP_APP_INFO[mapAppIndex];
}

var mapListEl=document.getElementById('map-list');
var mapStatusEl=document.getElementById('map-status');
var mapRows=[];
var mapListActive=false;
var mapListRemain=0;
var mapListTimer=null;
var mapSavePending=[];
var mapBulkQueue=[];      // 批量保存待发送的 {row,cmd}
var mapBulkActive=false;
var mapBulkCurrent=-1;
var mapBulkTotal=0;
var mapBulkSent=0;
var mapBulkFail=0;
var mapBulkTimer=null;

function setSubpageStatus(el,text,kind){
  el.textContent=text||'';
  el.className='subpage-status'+(kind?' '+kind:'');
}

function openCtrlPage(name){
  ctrlPage=name;
  document.getElementById('ctrl-main').classList.remove('active');
  document.getElementById('ctrl-map').classList.toggle('active',name==='map');
  document.getElementById('ctrl-config').classList.toggle('active',name==='config');
  if(name==='map')requestMapList();
  else requestConfigList();
}

function closeCtrlPage(){
  cancelMapBulk();
  cancelConfigBulk();
  ctrlPage='main';
  document.getElementById('ctrl-map').classList.remove('active');
  document.getElementById('ctrl-config').classList.remove('active');
  document.getElementById('ctrl-main').classList.add('active');
}

// ---------- 手势绑定 ----------
function mapAppInfo(app){
  if(MAP_APP_LOOKUP[app])return MAP_APP_LOOKUP[app];
  return {value:app,label:app+'（未知）',param:true,placeholder:'参数'};
}

function ensureMapAppOption(row,app){
  if(app===''||MAP_APP_LOOKUP[app]||row.knownApps[app])return;
  var opt=document.createElement('option');
  opt.value=app;
  opt.textContent=app+'（未知）';
  row.select.appendChild(opt);
  row.knownApps[app]=true;
}

function updateMapParamField(row,keepValue){
  var app=row.select.value;
  var info=mapAppInfo(app);
  // keepValue 仅在回显 map list 时为 true；用户切换 APP 时必须清空输入框，
  // 否则上一个应用的参数会残留，看起来像是新应用的参数。
  if(!keepValue)row.input.value='';
  if(app===''){
    row.input.disabled=true;
    row.input.placeholder='未绑定';
    return;
  }
  if(!info.param){
    row.input.disabled=true;
    row.input.placeholder='此应用无参数';
    return;
  }
  row.input.disabled=false;
  row.input.placeholder=info.placeholder||'参数';
}

function createMapRow(index){
  var rowEl=document.createElement('div');
  rowEl.className='cfg-row map-row';

  var label=document.createElement('span');
  label.className='cfg-label';
  label.textContent='手势 '+index;

  var select=document.createElement('select');
  for(var i=0;i<MAP_APP_INFO.length;i++){
    var opt=document.createElement('option');
    opt.value=MAP_APP_INFO[i].value;
    opt.textContent=MAP_APP_INFO[i].label;
    select.appendChild(opt);
  }

  var input=document.createElement('input');
  input.type='text';
  input.maxLength=127;
  input.autocomplete='off';
  input.spellcheck=false;

  var button=document.createElement('button');
  button.type='button';
  button.textContent='保存';

  rowEl.appendChild(label);
  rowEl.appendChild(select);
  rowEl.appendChild(input);
  rowEl.appendChild(button);
  mapListEl.appendChild(rowEl);

  var row={index:index,el:rowEl,select:select,input:input,button:button,knownApps:{},dirty:false};
  select.addEventListener('change',function(){
    row.dirty=true;
    updateMapParamField(row,false);
    rowEl.classList.remove('saved','failed');
  });
  input.addEventListener('input',function(){
    row.dirty=true;
    rowEl.classList.remove('saved','failed');
  });
  button.addEventListener('click',function(){saveMapRow(row)});
  updateMapParamField(row,false);
  return row;
}

function ensureMapRow(index){
  while(mapRows.length<=index)mapRows.push(createMapRow(mapRows.length));
  return mapRows[index];
}

function clearMapRows(count){
  mapListEl.replaceChildren();
  mapRows=[];
  for(var i=0;i<count;i++)ensureMapRow(i);
}

function requestMapList(){
  cancelMapBulk();
  mapSavePending=[];
  mapListActive=false;
  mapListRemain=0;
  if(mapListTimer){clearTimeout(mapListTimer);mapListTimer=null}
  setSubpageStatus(mapStatusEl,'正在读取手势绑定…','pending');
  if(!sendCmd('map list'))setSubpageStatus(mapStatusEl,'未连接，无法读取','error');
}

function scheduleMapListTimeout(){
  if(mapListTimer)clearTimeout(mapListTimer);
  mapListTimer=setTimeout(function(){
    mapListTimer=null;
    if(mapListActive){
      mapListActive=false;
      setSubpageStatus(mapStatusEl,'读取结束（部分行未收到，可点刷新重试）','error');
    }
  },1500);
}

function finishMapList(){
  if(mapListTimer){clearTimeout(mapListTimer);mapListTimer=null}
  setSubpageStatus(mapStatusEl,'已读取 '+mapRows.length+' 个手势绑定');
}

function parseMapBindingLine(line){
  if(line==='Gesture mappings are unavailable'){
    mapListActive=false;
    setSubpageStatus(mapStatusEl,'手势映射表不可用','error');
    return true;
  }
  var header=line.match(/^Gesture bindings \((\d+)\):$/);
  if(header){
    var count=parseInt(header[1],10);
    clearMapRows(count);
    mapListActive=count>0;
    mapListRemain=count;
    if(count>0)setSubpageStatus(mapStatusEl,'正在读取手势绑定…','pending');
    else setSubpageStatus(mapStatusEl,'当前没有可绑定的手势');
    scheduleMapListTimeout();
    return true;
  }
  if(!mapListActive)return false;
  var m=line.match(/^  \[(\d+)\] (.*)$/);
  if(!m)return false;
  var index=parseInt(m[1],10);
  var rest=m[2];
  var app='',param='';
  if(rest!=='<unbound>'){
    var parts=rest.match(/^(\S+)(?: "(.*)")?$/);
    if(parts){
      app=parts[1];
      if(parts[2]!==undefined)param=parts[2];
    }else{
      app=rest;
    }
  }
  var row=ensureMapRow(index);
  ensureMapAppOption(row,app);
  row.select.value=app;
  row.input.value=param;
  updateMapParamField(row,true);
  row.el.classList.remove('saved','failed');
  row.dirty=false;
  if(mapListRemain>0)mapListRemain--;
  if(mapListRemain===0){
    mapListActive=false;
    finishMapList();
  }else{
    scheduleMapListTimeout();
  }
  return true;
}

function mapRowCommand(row){
  var app=row.select.value;
  var info=mapAppInfo(app);
  var param=row.input.value;
  if(app==='')return {cmd:'map clear '+row.index};
  if(info.param&&param==='')return {needParam:true};
  if(info.param)return {cmd:'map bind '+row.index+' '+app+' '+param};
  return {cmd:'map bind '+row.index+' '+app};
}

function saveMapRow(row){
  if(mapBulkActive){
    setSubpageStatus(mapStatusEl,'正在批量保存，请稍候…','pending');
    return;
  }
  var plan=mapRowCommand(row);
  if(plan.needParam){
    row.el.classList.add('failed');
    setSubpageStatus(mapStatusEl,'手势 '+row.index+' 的 APP 需要参数','error');
    return;
  }
  if(!sendCmd(plan.cmd)){
    setSubpageStatus(mapStatusEl,'未连接，命令未发送','error');
    return;
  }
  row.el.classList.remove('saved','failed');
  mapSavePending.push(row.index);
  setSubpageStatus(mapStatusEl,'手势 '+row.index+'：已发送 '+plan.cmd,'pending');
}

// 一次性保存所有改动过的行。固件命令队列只有 4 格且满了会直接拒绝，
// 因此这里逐条发送，收到上一条结果后再发下一条。
function saveAllMap(){
  if(mapBulkActive){
    setSubpageStatus(mapStatusEl,'正在批量保存，请稍候…','pending');
    return;
  }
  if(mapListActive){
    setSubpageStatus(mapStatusEl,'正在读取手势绑定，请稍候…','pending');
    return;
  }
  if(mapSavePending.length){
    setSubpageStatus(mapStatusEl,'有保存命令尚未完成，请稍候再试','pending');
    return;
  }
  var queue=[];
  var badRow=null;
  for(var i=0;i<mapRows.length;i++){
    var row=mapRows[i];
    if(!row.dirty)continue;
    var plan=mapRowCommand(row);
    if(plan.needParam){badRow=row;break}
    queue.push({row:row,cmd:plan.cmd});
  }
  if(badRow){
    badRow.el.classList.remove('saved');
    badRow.el.classList.add('failed');
    setSubpageStatus(mapStatusEl,'手势 '+badRow.index+' 的 APP 需要参数，未保存任何修改','error');
    return;
  }
  if(!queue.length){
    setSubpageStatus(mapStatusEl,'没有需要保存的修改','');
    return;
  }
  mapBulkQueue=queue;
  mapBulkTotal=queue.length;
  mapBulkSent=0;
  mapBulkFail=0;
  mapBulkActive=true;
  sendNextMapBulk();
}

function sendNextMapBulk(){
  if(mapBulkTimer){clearTimeout(mapBulkTimer);mapBulkTimer=null}
  if(!mapBulkActive)return;
  if(!mapBulkQueue.length){
    mapBulkActive=false;
    mapBulkCurrent=-1;
    if(mapBulkFail>0)setSubpageStatus(mapStatusEl,'批量保存完成：'+mapBulkSent+' 项中 '+mapBulkFail+' 项失败，失败行已标红','error');
    else setSubpageStatus(mapStatusEl,'批量保存完成：共 '+mapBulkSent+' 项，全部成功','');
    return;
  }
  var item=mapBulkQueue.shift();
  if(!sendCmd(item.cmd)){
    mapBulkActive=false;
    mapBulkQueue=[];
    mapBulkCurrent=-1;
    mapSavePending=[];
    setSubpageStatus(mapStatusEl,'未连接，批量保存已中止（已发送 '+mapBulkSent+'/'+mapBulkTotal+'）','error');
    return;
  }
  mapBulkSent++;
  mapBulkCurrent=item.row.index;
  mapSavePending.push(item.row.index);
  setSubpageStatus(mapStatusEl,'正在保存 '+mapBulkSent+'/'+mapBulkTotal+'：手势 '+item.row.index,'pending');
  // 设备无响应时按失败处理，避免整个批量流程卡住
  mapBulkTimer=setTimeout(function(){
    mapBulkTimer=null;
    if(mapBulkActive)markMapRowSaved(item.row.index,false,'设备无响应');
  },3000);
}

function cancelMapBulk(){
  if(!mapBulkActive)return;
  if(mapBulkTimer){clearTimeout(mapBulkTimer);mapBulkTimer=null}
  mapBulkActive=false;
  mapBulkQueue=[];
  mapBulkCurrent=-1;
  mapSavePending=[];
}

function markMapRowSaved(index,ok,detail){
  var pos=mapSavePending.indexOf(index);
  if(pos>=0)mapSavePending.splice(pos,1);
  var row=mapRows[index];
  if(row){
    row.el.classList.remove(ok?'failed':'saved');
    row.el.classList.add(ok?'saved':'failed');
    if(ok)row.dirty=false;
  }
  if(mapBulkActive){
    // 只让当前在途命令的回应推进队列，迟到的旧回应不重复触发发送
    if(mapBulkCurrent===index){
      mapBulkCurrent=-1;
      if(!ok)mapBulkFail++;
      sendNextMapBulk();
    }
    return;
  }
  setSubpageStatus(mapStatusEl,'手势 '+index+(ok?' 已保存':' 保存失败'+(detail?'：'+detail:'')),ok?'':'error');
}

function processMapResult(line){
  var m=line.match(/^Saved: gesture (\d+)/);
  if(m){markMapRowSaved(parseInt(m[1],10),true);return true}
  m=line.match(/^Cleared: gesture (\d+)/);
  if(m){markMapRowSaved(parseInt(m[1],10),true);return true}
  if(line==='Gesture mappings reset to defaults'){
    cancelMapBulk();
    mapSavePending=[];
    setSubpageStatus(mapStatusEl,'已恢复默认手势绑定，正在刷新…','pending');
    setTimeout(requestMapList,300);
    return true;
  }
  if(line.indexOf('Error:')===0||line.indexOf('[Web] Command rejected')===0){
    var index=mapSavePending.length?mapSavePending[0]:-1;
    var detail=line.substring(6).trim();
    if(index>=0&&mapRows[index]){
      markMapRowSaved(index,false,detail);
    }else{
      setSubpageStatus(mapStatusEl,line,'error');
    }
    return true;
  }
  return false;
}

function mapReset(){
  if(mapBulkActive){
    setSubpageStatus(mapStatusEl,'正在批量保存，请稍候…','pending');
    return;
  }
  if(!confirm('恢复默认的手势绑定？当前绑定将被覆盖。'))return;
  if(!sendCmd('map reset')){
    setSubpageStatus(mapStatusEl,'未连接，命令未发送','error');
    return;
  }
  setSubpageStatus(mapStatusEl,'正在恢复默认手势绑定…','pending');
}

// ---------- 参数配置 ----------
var CONFIG_FIELDS=[
  {key:'ble',label:'BLE 名称',cmd:'ble',saved:'BLE name',max:29,hint:'1-29 字节'},
  {key:'ap_ssid',label:'AP SSID',cmd:'ap',saved:'AP SSID',max:32,hint:'1-32 字节'},
  {key:'ap_pass',label:'AP 密码',cmd:'ap-pass',saved:'AP password',max:63,hint:'8-63 字节'},
  {key:'wifi_ssid',label:'WiFi SSID',cmd:'wifi',saved:'WiFi SSID',max:32,hint:'1-32 字节'},
  {key:'wifi_pass',label:'WiFi 密码',cmd:'wifi-pass',saved:'WiFi password',max:63,hint:'8-63 字节'},
  {key:'mqtt_host',label:'MQTT 主机',cmd:'mqtt',saved:'MQTT host',max:63,hint:'IP 或域名，不能含空格'},
  {key:'mqtt_port',label:'MQTT 端口',cmd:'mqtt-port',saved:'MQTT port',max:5,hint:'1-65535'},
  {key:'mqtt_user',label:'MQTT 用户',cmd:'mqtt-user',saved:'MQTT user',max:31,hint:'用户名'},
  {key:'mqtt_pass',label:'MQTT 密码',cmd:'mqtt-pass',saved:'MQTT password',max:63,hint:'密码'}
];
var CONFIG_LIST_FIELDS={
  'BLE name':'ble','AP SSID':'ap_ssid','AP PASS':'ap_pass',
  'WiFi SSID':'wifi_ssid','WiFi PASS':'wifi_pass',
  'MQTT host':'mqtt_host','MQTT port':'mqtt_port',
  'MQTT user':'mqtt_user','MQTT pass':'mqtt_pass'
};

var configListEl=document.getElementById('config-list');
var configStatusEl=document.getElementById('config-status');
var configRows={};
var configListActive=false;
var configListRemain=0;
var configListTimer=null;
var configPending=null;
var configBulkQueue=[];   // 批量保存待发送的 {row,cmd}
var configBulkActive=false;
var configBulkCurrent=null;
var configBulkTotal=0;
var configBulkSent=0;
var configBulkFail=0;
var configBulkTimer=null;

function buildConfigRows(){
  for(var i=0;i<CONFIG_FIELDS.length;i++){
    var field=CONFIG_FIELDS[i];
    var rowEl=document.createElement('div');
    rowEl.className='cfg-row config-row';

    var label=document.createElement('span');
    label.className='cfg-label';
    label.textContent=field.label;

    var input=document.createElement('input');
    input.type='text';
    input.maxLength=field.max;
    input.autocomplete='off';
    input.spellcheck=false;
    if(field.key==='mqtt_port')input.inputMode='numeric';
    input.placeholder=field.hint||'';

    var button=document.createElement('button');
    button.type='button';
    button.textContent='保存';

    rowEl.appendChild(label);
    rowEl.appendChild(input);
    rowEl.appendChild(button);
    configListEl.appendChild(rowEl);

    var row={field:field,el:rowEl,input:input,button:button,dirty:false};
    registerConfigRowEvents(row);
    configRows[field.key]=row;
  }
}

function registerConfigRowEvents(row){
  row.button.addEventListener('click',function(){saveConfigRow(row)});
  row.input.addEventListener('input',function(){
    row.dirty=true;
    row.el.classList.remove('saved','failed');
  });
}

function requestConfigList(){
  cancelConfigBulk();
  configPending=null;
  configListActive=false;
  configListRemain=0;
  if(configListTimer){clearTimeout(configListTimer);configListTimer=null}
  setSubpageStatus(configStatusEl,'正在读取设备配置…','pending');
  if(!sendCmd('config list'))setSubpageStatus(configStatusEl,'未连接，无法读取','error');
}

function scheduleConfigListTimeout(){
  if(configListTimer)clearTimeout(configListTimer);
  configListTimer=setTimeout(function(){
    configListTimer=null;
    if(configListActive){
      configListActive=false;
      setSubpageStatus(configStatusEl,'读取结束（部分字段未收到，可点刷新重试）','error');
    }
  },1500);
}

function parseConfigListLine(line){
  if(line==='Device config:'){
    configListActive=true;
    configListRemain=CONFIG_FIELDS.length;
    setSubpageStatus(configStatusEl,'正在读取设备配置…','pending');
    scheduleConfigListTimeout();
    return true;
  }
  if(!configListActive)return false;
  var m=line.match(/^  (BLE name|AP SSID|AP PASS|WiFi SSID|WiFi PASS|MQTT host|MQTT port|MQTT user|MQTT pass)\s*:\s*(.*)$/);
  if(!m)return false;
  var key=CONFIG_LIST_FIELDS[m[1]];
  var raw=m[2];
  var value=raw;
  // config list 用引号包裹字符串；用首尾引号剥离，值内部的引号原样保留
  if(raw.length>=2&&raw.charAt(0)==='"'&&raw.charAt(raw.length-1)==='"')value=raw.slice(1,-1);
  var row=configRows[key];
  if(row){
    row.input.value=value;
    row.el.classList.remove('saved','failed');
    row.dirty=false;
  }
  if(configListRemain>0)configListRemain--;
  if(configListRemain===0){
    configListActive=false;
    if(configListTimer){clearTimeout(configListTimer);configListTimer=null}
    setSubpageStatus(configStatusEl,'已读取设备配置');
  }else{
    scheduleConfigListTimeout();
  }
  return true;
}

function configRowCommand(row){
  var field=row.field;
  var value=row.input.value;
  if(value==='')return {needValue:true};
  if(field.key==='mqtt_port'&&!/^[0-9]+$/.test(value))return {badPort:true};
  return {cmd:'config '+field.cmd+' '+value};
}

function saveConfigRow(row){
  if(configBulkActive){
    setSubpageStatus(configStatusEl,'正在批量保存，请稍候…','pending');
    return;
  }
  var plan=configRowCommand(row);
  if(plan.needValue){
    row.el.classList.add('failed');
    setSubpageStatus(configStatusEl,row.field.label+' 不能为空（清空配置请用上方清除/恢复按钮）','error');
    return;
  }
  if(plan.badPort){
    row.el.classList.add('failed');
    setSubpageStatus(configStatusEl,'MQTT 端口必须是数字','error');
    return;
  }
  if(!sendCmd(plan.cmd,true)){
    setSubpageStatus(configStatusEl,'未连接，命令未发送','error');
    return;
  }
  row.el.classList.remove('saved','failed');
  configPending=row.field.key;
  setSubpageStatus(configStatusEl,row.field.label+'：正在保存…','pending');
}

// 与手势绑定页一致：逐条发送并等待上一条结果，避免塞满固件命令队列
function saveAllConfig(){
  if(configBulkActive){
    setSubpageStatus(configStatusEl,'正在批量保存，请稍候…','pending');
    return;
  }
  if(configListActive){
    setSubpageStatus(configStatusEl,'正在读取设备配置，请稍候…','pending');
    return;
  }
  if(configPending){
    setSubpageStatus(configStatusEl,'有保存命令尚未完成，请稍候再试','pending');
    return;
  }
  var queue=[];
  var badRow=null;
  var badText='';
  for(var i=0;i<CONFIG_FIELDS.length;i++){
    var row=configRows[CONFIG_FIELDS[i].key];
    if(!row||!row.dirty)continue;
    var plan=configRowCommand(row);
    if(plan.needValue){
      badRow=row;
      badText=row.field.label+' 不能为空（清空配置请用上方清除/恢复按钮），未保存任何修改';
      break;
    }
    if(plan.badPort){
      badRow=row;
      badText='MQTT 端口必须是数字，未保存任何修改';
      break;
    }
    queue.push({row:row,cmd:plan.cmd});
  }
  if(badRow){
    badRow.el.classList.remove('saved');
    badRow.el.classList.add('failed');
    setSubpageStatus(configStatusEl,badText,'error');
    return;
  }
  if(!queue.length){
    setSubpageStatus(configStatusEl,'没有需要保存的修改','');
    return;
  }
  configBulkQueue=queue;
  configBulkTotal=queue.length;
  configBulkSent=0;
  configBulkFail=0;
  configBulkActive=true;
  sendNextConfigBulk();
}

function sendNextConfigBulk(){
  if(configBulkTimer){clearTimeout(configBulkTimer);configBulkTimer=null}
  if(!configBulkActive)return;
  if(!configBulkQueue.length){
    configBulkActive=false;
    configBulkCurrent=null;
    if(configBulkFail>0)setSubpageStatus(configStatusEl,'批量保存完成：'+configBulkSent+' 项中 '+configBulkFail+' 项失败，失败行已标红（重启后生效）','error');
    else setSubpageStatus(configStatusEl,'批量保存完成：共 '+configBulkSent+' 项，全部成功（重启后生效）','');
    return;
  }
  var item=configBulkQueue.shift();
  if(!sendCmd(item.cmd,true)){
    configBulkActive=false;
    configBulkQueue=[];
    configBulkCurrent=null;
    configPending=null;
    setSubpageStatus(configStatusEl,'未连接，批量保存已中止（已发送 '+configBulkSent+'/'+configBulkTotal+'）','error');
    return;
  }
  configBulkSent++;
  configBulkCurrent=item.row.field.key;
  configPending=item.row.field.key;
  setSubpageStatus(configStatusEl,'正在保存 '+configBulkSent+'/'+configBulkTotal+'：'+item.row.field.label,'pending');
  // 设备无响应时按失败处理，避免整个批量流程卡住
  configBulkTimer=setTimeout(function(){
    configBulkTimer=null;
    if(configBulkActive)markConfigRowSaved(item.row.field.key,false,'保存超时，设备无响应');
  },3000);
}

function cancelConfigBulk(){
  if(!configBulkActive)return;
  if(configBulkTimer){clearTimeout(configBulkTimer);configBulkTimer=null}
  configBulkActive=false;
  configBulkQueue=[];
  configBulkCurrent=null;
  configPending=null;
}

function markConfigRowSaved(key,ok,detail){
  var row=configRows[key];
  if(configPending===key)configPending=null;
  if(row){
    row.el.classList.remove(ok?'failed':'saved');
    row.el.classList.add(ok?'saved':'failed');
    if(ok)row.dirty=false;
  }
  if(configBulkActive){
    // 只让当前在途命令的回应推进队列，迟到的旧回应不重复触发发送
    if(configBulkCurrent===key){
      configBulkCurrent=null;
      if(!ok)configBulkFail++;
      sendNextConfigBulk();
    }
    return;
  }
  if(ok)setSubpageStatus(configStatusEl,(row?row.field.label:'配置')+' 已保存（重启后生效）');
  else setSubpageStatus(configStatusEl,detail||'保存失败','error');
}

function processConfigResult(line){
  if(line.indexOf('Saved: ')===0){
    var rest=line.substring(7);
    for(var i=0;i<CONFIG_FIELDS.length;i++){
      var field=CONFIG_FIELDS[i];
      if(rest.indexOf(field.saved)===0){
        markConfigRowSaved(field.key,true);
        return true;
      }
    }
    return false;
  }
  if(line.indexOf('Cleared: WiFi config')===0){
    clearConfigInputs(['wifi_ssid','wifi_pass']);
    setSubpageStatus(configStatusEl,'已清除 WiFi 配置（重启后生效）');
    return true;
  }
  if(line.indexOf('Cleared: MQTT config')===0){
    clearConfigInputs(['mqtt_host','mqtt_user','mqtt_pass']);
    configRows.mqtt_port.input.value='1883';
    configRows.mqtt_port.dirty=false;
    setSubpageStatus(configStatusEl,'已清除 MQTT 配置（重启后生效）');
    return true;
  }
  if(line.indexOf('Device config reset to defaults')===0){
    cancelConfigBulk();
    configPending=null;
    setSubpageStatus(configStatusEl,'已恢复默认配置，正在刷新…','pending');
    setTimeout(requestConfigList,300);
    return true;
  }
  if(line.indexOf('Error:')===0||line.indexOf('[Web] Command rejected')===0){
    if(configPending&&configRows[configPending]){
      markConfigRowSaved(configPending,false,line);
      return true;
    }
    return false;
  }
  return false;
}

function clearConfigInputs(keys){
  for(var i=0;i<keys.length;i++){
    var row=configRows[keys[i]];
    if(row){
      row.input.value='';
      row.el.classList.remove('saved','failed');
      row.dirty=false;
    }
  }
}

function configClear(target){
  if(configBulkActive){
    setSubpageStatus(configStatusEl,'正在批量保存，请稍候…','pending');
    return;
  }
  var label=target==='wifi'?'WiFi':'MQTT';
  if(!confirm('清除 '+label+' 配置？重启后生效。'))return;
  if(!sendCmd('config clear '+target)){
    setSubpageStatus(configStatusEl,'未连接，命令未发送','error');
    return;
  }
  setSubpageStatus(configStatusEl,'正在清除 '+label+' 配置…','pending');
}

function configReset(){
  if(configBulkActive){
    setSubpageStatus(configStatusEl,'正在批量保存，请稍候…','pending');
    return;
  }
  if(!confirm('恢复默认配置？BLE / AP / WiFi / MQTT 设置都会重置，重启后生效。'))return;
  if(!sendCmd('config reset')){
    setSubpageStatus(configStatusEl,'未连接，命令未发送','error');
    return;
  }
  setSubpageStatus(configStatusEl,'正在恢复默认配置…','pending');
}

buildConfigRows();

// 统一处理设备回显：只解析当前打开页面关心的行
function processDeviceLine(line){
  if(ctrlPage==='map')return parseMapBindingLine(line)||processMapResult(line);
  if(ctrlPage==='config')return parseConfigListLine(line)||processConfigResult(line);
  return false;
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
    // 手势绑定 / 参数配置页面：解析 map list / config list 与保存结果
    if(processDeviceLine(l))continue;
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

// AP 侧 WebSocket 客户端登记：自愈只断开走 SoftAP 的页面，保留 STA 页面。
// 只记录 client id——客户端对象可能随时被 AsyncTCP 任务销毁，不能存指针。
#define AP_WEB_MAX_AP_CLIENTS 8
static uint32_t s_ap_client_ids[AP_WEB_MAX_AP_CLIENTS] = {0};
static SemaphoreHandle_t s_ap_client_lock = nullptr;

static bool ip_is_ap_side(const IPAddress &ip)
{
  const IPAddress ap = WiFi.softAPIP();
  return ip[0] == ap[0] && ip[1] == ap[1] && ip[2] == ap[2];
}

static void ap_web_track_client(AsyncWebSocketClient *client)
{
  if (s_ap_client_lock == nullptr || !ip_is_ap_side(client->remoteIP()))
    return;

  // SoftAP 最多 4 个客户端，8 个槽位足够；满了就放弃登记
  xSemaphoreTake(s_ap_client_lock, portMAX_DELAY);
  for (int i = 0; i < AP_WEB_MAX_AP_CLIENTS; ++i)
  {
    if (s_ap_client_ids[i] == 0)
    {
      s_ap_client_ids[i] = client->id();
      break;
    }
  }
  xSemaphoreGive(s_ap_client_lock);
}

static void ap_web_untrack_client(uint32_t id)
{
  if (s_ap_client_lock == nullptr)
    return;

  xSemaphoreTake(s_ap_client_lock, portMAX_DELAY);
  for (int i = 0; i < AP_WEB_MAX_AP_CLIENTS; ++i)
  {
    if (s_ap_client_ids[i] == id)
      s_ap_client_ids[i] = 0;
  }
  xSemaphoreGive(s_ap_client_lock);
}

static void ap_web_clear_tracked(void)
{
  if (s_ap_client_lock == nullptr)
    return;

  xSemaphoreTake(s_ap_client_lock, portMAX_DELAY);
  memset(s_ap_client_ids, 0, sizeof(s_ap_client_ids));
  xSemaphoreGive(s_ap_client_lock);
}

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
    // 不设置服务端 keepAlive：页面每 1s 发应用层心跳，额外 PING 只会增加 AP 下行流量
    ap_web_track_client(client);
    DualSerial.printf("[Web] Client #%u connected\n", client->id());
    break;

  case WS_EVT_DISCONNECT:
    ap_web_untrack_client(client->id());
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
          // 回显 ":<seq>:<ts>" 供浏览器计算 RTT，并追加设备状态：
          // :<蓝牙0/1/2>:<ESP-NOW 0/1>:<电量%>:<电压mV>:<是否充电>:<AP 0/1>:<WiFi 0/1/2>
          // AP/WiFi 追加在末尾，旧页面只读前 8 段，固件更新后可继续工作
          int soc = (int)(HAL::MAX17048_getSOC() + 0.5f);
          if (soc < 0)
            soc = 0;
          else if (soc > 100)
            soc = 100;
          int mv = (int)(HAL::MAX17048_getVoltage() * 1000.0f + 0.5f);
          // 蓝牙：0=已关闭，1=已开启但未连接，2=已连接
          int ble = !ble_is_enabled() ? 0 : (ble_is_connected() ? 2 : 1);
          // WiFi：0=未配置/已关闭，1=已开启但未连接，2=已连接
          int wifi = !network_is_sta_enabled() ? 0 : (network_is_sta_connected() ? 2 : 1);

          char status[40];
          int statusLen = snprintf(status, sizeof(status), ":%d:%d:%d:%d:%d:%d:%d",
                                   ble, espnow_is_running() ? 1 : 0,
                                   soc, mv, HAL::power_getChargeStatus() ? 1 : 0,
                                   ap_is_running() ? 1 : 0, wifi);
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

  if (s_ap_client_lock == nullptr)
    s_ap_client_lock = xSemaphoreCreateMutex();

  if (!webRoutesSet)
  {
    webSocket.onEvent(onWsEvent);
    webServer.addHandler(&webSocket);

    // 根路径：返回内嵌网页
    // 禁止缓存，确保固件更新后浏览器拿到的是最新页面逻辑
    // 用 _P 版本直接从 flash 流式发送，避免把整页复制到堆上（AP 模式堆紧张）
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
                   AsyncWebServerResponse *response =
                       request->beginResponse_P(200, "text/html; charset=utf-8", WEB_PAGE);
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

  ap_web_clear_tracked();
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

void ap_web_close_all(void)
{
  if (!webRunning)
    return;

  if (s_ap_client_lock == nullptr)
  {
    webSocket.closeAll();
    return;
  }

  uint32_t ids[AP_WEB_MAX_AP_CLIENTS];
  xSemaphoreTake(s_ap_client_lock, portMAX_DELAY);
  memcpy(ids, s_ap_client_ids, sizeof(ids));
  memset(s_ap_client_ids, 0, sizeof(s_ap_client_ids));
  xSemaphoreGive(s_ap_client_lock);

  // close() 只是排队 close 帧；发送窗口卡死的客户端只能靠射频重启
  // 和 AsyncTCP 5s ACK 超时回收，应用层无法安全地拿到对象做 abort()。
  for (int i = 0; i < AP_WEB_MAX_AP_CLIENTS; ++i)
  {
    if (ids[i] != 0)
      webSocket.close(ids[i]);
  }
}
