#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include "web_ui.h"

#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *WEB_INDEX_HTML =
"<!DOCTYPE html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Terminal BMT VCU</title><link rel=\"stylesheet\" href=\"/styles.css\"></head><body>"
"<div class=\"splash\" id=\"splash\"><div class=\"box\"><div class=\"ring\"></div><div class=\"core\">BMT</div></div><h1>TERMINAL BMT</h1><p>Booting VCU interface...</p></div>"
"<div class=\"app\" id=\"app\">"
"<header><div><p class=\"eyebrow\">Terminal BMT</p><h1>VCU Live Dashboard</h1><p class=\"sub\">Telemetry, CAN control and DBC configuration in one local interface.</p></div><div class=\"meta\"><button id=\"aboutButton\">About</button><span id=\"serverState\">Conectando...</span><span id=\"canState\">CAN offline</span><span id=\"lastUpdate\">Sem dados</span></div></header>"
"<nav><button class=\"tab active\" data-view=\"dashboard\">Dashboard</button><button class=\"tab\" data-view=\"serial\">Serial Monitor</button><button class=\"tab\" data-view=\"canmonitor\">CAN Monitor</button><button class=\"tab\" data-view=\"settings\">Settings</button></nav>"
"<section class=\"view active\" id=\"dashboardView\"><div class=\"dashboard-nav\"><button class=\"dash-tab active\" data-dash=\"vcu\" id=\"dashTabVcu\">VCU</button><button class=\"dash-tab\" data-dash=\"inverters\" id=\"dashTabInverters\">Inverters</button></div>"
"<div class=\"dash-panel active\" id=\"vcuPanel\"><div class=\"dash-overlay-shell\" id=\"vcuOverlayShell\"><div class=\"card quick-head\"><div class=\"quick-status\"><span id=\"vcuTopCan\">CAN offline</span><span id=\"vcuTopDbc\">DBC VCU: off</span><span id=\"vcuTopFresh\">Freshness: no data</span><span id=\"vcuTopUpdate\">Last: --</span></div><div class=\"quick-actions\"><button class=\"quick-nav-settings\">Settings</button><button data-action=\"can_open\">Open CAN</button><button data-action=\"can_start\">Start CAN</button><button data-action=\"can_stop\">Stop CAN</button></div></div>"
"<div class=\"kpis\"><div class=\"card\"><span>VCU State</span><strong id=\"vcu_state_kpi\">N/A</strong><small id=\"command_source_kpi\">Source: N/A</small></div><div class=\"card\"><span>Driver Request</span><strong id=\"longitudinal_request_kpi\">0.0 %</strong><div class=\"bar\"><div id=\"longitudinal_bar\"></div></div></div><div class=\"card\"><span>Communication</span><div class=\"row\"><b>RX</b><strong id=\"rx_count\">0</strong></div><div class=\"row\"><b>DBC</b><strong id=\"dbc_loaded\">NO</strong></div><div class=\"row\"><b>Freshness</b><strong id=\"freshness\">No data</strong></div></div></div>"
"<div class=\"grid\">"
"<div class=\"card\"><h2>System State</h2><div class=\"kv\"><span>VCU State</span><strong id=\"vcu_state\">N/A</strong></div><div class=\"kv\"><span>Command Source</span><strong id=\"command_source\">N/A</strong></div><div class=\"kv\"><span>Torque Allowed</span><strong id=\"torque_allowed\">NO</strong></div><div class=\"kv\"><span>Torque Limited</span><strong id=\"torque_limited\">NO</strong></div><div class=\"kv\"><span>Status Version</span><strong id=\"status_version\">0</strong></div><div class=\"kv\"><span>Alive Counter</span><strong id=\"alive_counter\">0</strong></div></div>"
"<div class=\"card\"><h2>Driver Inputs</h2><div class=\"bar small\"><div id=\"longitudinal_bar_small\"></div></div><div class=\"kv\"><span>Longitudinal Request</span><strong id=\"longitudinal_request\">0.0 %</strong></div><div class=\"kv\"><span>Throttle State</span><strong id=\"throttle_state\">N/A</strong></div><div class=\"kv\"><span>Brake State</span><strong id=\"brake_state\">N/A</strong></div><div class=\"kv\"><span>Pedals Valid</span><strong id=\"pedals_valid\">NO</strong></div><div class=\"kv\"><span>Pedals Tx Enabled</span><strong id=\"pedals_tx_enabled\">NO</strong></div><div class=\"kv\"><span>Pedals Alive</span><strong id=\"pedals_alive_counter\">0</strong></div></div>"
"<div class=\"card\"><h2>Health</h2><div class=\"kv\"><span>Throttle Health</span><strong id=\"throttle_health\">N/A</strong></div><div class=\"kv\"><span>Brake Health</span><strong id=\"brake_health\">N/A</strong></div><div class=\"kv\"><span>Throttle Raw Health</span><strong id=\"throttle_raw_health\">N/A</strong></div><div class=\"kv\"><span>Brake Raw Health</span><strong id=\"brake_raw_health\">N/A</strong></div></div>"
"<div class=\"card\"><h2>Features</h2><div class=\"flags\"><span class=\"flag\" id=\"regen_enabled\">REGEN</span><span class=\"flag\" id=\"yaw_enabled\">YAW</span><span class=\"flag\" id=\"traction_enabled\">TRACTION</span><span class=\"flag\" id=\"driver_takeover\">TAKEOVER</span><span class=\"flag\" id=\"remote_authorized\">REMOTE</span></div></div>"
"<div class=\"card wide\"><h2>Faults and Communication</h2><div class=\"faults\"><div class=\"kv\"><span>HW Fault</span><strong id=\"hw_fault\">NO</strong></div><div class=\"kv\"><span>CAN Fault</span><strong id=\"can_fault\">NO</strong></div><div class=\"kv\"><span>Inverter Fault</span><strong id=\"inverter_handshake_fault\">NO</strong></div><div class=\"kv\"><span>Startup Failed</span><strong id=\"startup_failed\">NO</strong></div><div class=\"kv\"><span>TCU Heartbeat</span><strong id=\"tcu_heartbeat_ok\">NO</strong></div><div class=\"kv\"><span>HPC Heartbeat</span><strong id=\"hpc_heartbeat_ok\">NO</strong></div><div class=\"kv\"><span>Inverter Handshake</span><strong id=\"inverter_handshake_ok\">NO</strong></div><div class=\"kv\"><span>Reset Request</span><strong id=\"controlled_reset_req\">NO</strong></div><div class=\"kv\"><span>Brake Dominance</span><strong id=\"brake_dominance_hard\">NO</strong></div></div></div>"
"</div></div><div class=\"dash-panel-off hidden\" id=\"vcuDashOff\"><div class=\"dash-panel-off-card\"><h2>VCU Dashboard Off</h2><p>Para habilitar o dashboard VCU, desabilite o CAN Monitor.</p></div></div></div><div class=\"dash-panel\" id=\"invertersPanel\"><div class=\"dash-overlay-shell\" id=\"invertersOverlayShell\"><div class=\"card quick-head\"><div class=\"quick-status\"><span id=\"invTopCan\">CAN offline</span><span id=\"invTopDbc\">DBC Inverter: off</span><span id=\"invTopLeftBase\">Left base: --</span><span id=\"invTopRightBase\">Right base: --</span><span id=\"invTopUpdate\">Last: --</span></div><div class=\"quick-actions\"><button class=\"quick-nav-settings\">Settings</button><button data-action=\"can_open\">Open CAN</button><button data-action=\"can_start\">Start CAN</button><button data-action=\"can_stop\">Stop CAN</button></div></div><div class=\"inverter-grid\"><div class=\"card inverter-card\"><div class=\"inverter-head\"><div><p class=\"eyebrow\">Inverter A</p><h2 id=\"invLeftState\">Awaiting Base</h2></div><div class=\"meta-stack\"><span id=\"invLeftOnline\">Offline</span><span id=\"invLeftUpdate\">No data</span></div></div><div class=\"kpi-pair\"><div class=\"mini-kpi\"><span>Speed</span><strong id=\"invLeftSpeed\">0 rpm</strong></div><div class=\"mini-kpi\"><span>DC Voltage</span><strong id=\"invLeftVdc\">0.0 V</strong></div><div class=\"mini-kpi\"><span>AC Current</span><strong id=\"invLeftIac\">0.0 A</strong></div><div class=\"mini-kpi\"><span>IGBT Temp</span><strong id=\"invLeftTigbt\">0.0 C</strong></div></div><div class=\"dual-kv\"><div class=\"kv\"><span>Control Mode</span><strong id=\"invLeftMode\">N/A</strong></div><div class=\"kv\"><span>Direction</span><strong id=\"invLeftDirection\">N/A</strong></div><div class=\"kv\"><span>Speed Ref</span><strong id=\"invLeftSpeedRef\">0 rpm</strong></div><div class=\"kv\"><span>Torque Ref</span><strong id=\"invLeftTorqueRef\">0.0 %</strong></div><div class=\"kv\"><span>AC Voltage</span><strong id=\"invLeftVac\">0.0 V</strong></div><div class=\"kv\"><span>Motor Temp</span><strong id=\"invLeftTmotor\">0.0 C</strong></div><div class=\"kv\"><span>Aux Temp</span><strong id=\"invLeftTmotorAux\">0.0 C</strong></div><div class=\"kv\"><span>Event</span><strong id=\"invLeftEvent\">N/A</strong></div></div><div class=\"flags inverter-flags\"><span class=\"flag\" id=\"invLeftOnOff\">ENABLE</span><span class=\"flag\" id=\"invLeftOverSpeed\">OVERSPEED</span><span class=\"flag\" id=\"invLeftOverVoltage\">OVERVOLT</span><span class=\"flag\" id=\"invLeftUnderVoltage\">UNDERVOLT</span><span class=\"flag\" id=\"invLeftOverTempMotor\">HOT MOTOR</span><span class=\"flag\" id=\"invLeftOverTempIgbt\">HOT IGBT</span><span class=\"flag\" id=\"invLeftFocError\">FOC</span><span class=\"flag\" id=\"invLeftRunError\">RUN</span><span class=\"flag\" id=\"invLeftInitError\">INIT</span></div><div class=\"kpi-pair compact\"><div class=\"mini-kpi\"><span>Gate Drivers Ready</span><strong id=\"invLeftGdReady\">0</strong></div><div class=\"mini-kpi\"><span>Gate Driver Faults</span><strong id=\"invLeftGdFaults\">0</strong></div><div class=\"mini-kpi\"><span>Measures ID</span><strong id=\"invLeftMeasuresId\">0x00000000</strong></div><div class=\"mini-kpi\"><span>States ID</span><strong id=\"invLeftStatesId\">0x00000000</strong></div></div></div><div class=\"card inverter-card\"><div class=\"inverter-head\"><div><p class=\"eyebrow\">Inverter B</p><h2 id=\"invRightState\">Awaiting Base</h2></div><div class=\"meta-stack\"><span id=\"invRightOnline\">Offline</span><span id=\"invRightUpdate\">No data</span></div></div><div class=\"kpi-pair\"><div class=\"mini-kpi\"><span>Speed</span><strong id=\"invRightSpeed\">0 rpm</strong></div><div class=\"mini-kpi\"><span>DC Voltage</span><strong id=\"invRightVdc\">0.0 V</strong></div><div class=\"mini-kpi\"><span>AC Current</span><strong id=\"invRightIac\">0.0 A</strong></div><div class=\"mini-kpi\"><span>IGBT Temp</span><strong id=\"invRightTigbt\">0.0 C</strong></div></div><div class=\"dual-kv\"><div class=\"kv\"><span>Control Mode</span><strong id=\"invRightMode\">N/A</strong></div><div class=\"kv\"><span>Direction</span><strong id=\"invRightDirection\">N/A</strong></div><div class=\"kv\"><span>Speed Ref</span><strong id=\"invRightSpeedRef\">0 rpm</strong></div><div class=\"kv\"><span>Torque Ref</span><strong id=\"invRightTorqueRef\">0.0 %</strong></div><div class=\"kv\"><span>AC Voltage</span><strong id=\"invRightVac\">0.0 V</strong></div><div class=\"kv\"><span>Motor Temp</span><strong id=\"invRightTmotor\">0.0 C</strong></div><div class=\"kv\"><span>Aux Temp</span><strong id=\"invRightTmotorAux\">0.0 C</strong></div><div class=\"kv\"><span>Event</span><strong id=\"invRightEvent\">N/A</strong></div></div><div class=\"flags inverter-flags\"><span class=\"flag\" id=\"invRightOnOff\">ENABLE</span><span class=\"flag\" id=\"invRightOverSpeed\">OVERSPEED</span><span class=\"flag\" id=\"invRightOverVoltage\">OVERVOLT</span><span class=\"flag\" id=\"invRightUnderVoltage\">UNDERVOLT</span><span class=\"flag\" id=\"invRightOverTempMotor\">HOT MOTOR</span><span class=\"flag\" id=\"invRightOverTempIgbt\">HOT IGBT</span><span class=\"flag\" id=\"invRightFocError\">FOC</span><span class=\"flag\" id=\"invRightRunError\">RUN</span><span class=\"flag\" id=\"invRightInitError\">INIT</span></div><div class=\"kpi-pair compact\"><div class=\"mini-kpi\"><span>Gate Drivers Ready</span><strong id=\"invRightGdReady\">0</strong></div><div class=\"mini-kpi\"><span>Gate Driver Faults</span><strong id=\"invRightGdFaults\">0</strong></div><div class=\"mini-kpi\"><span>Measures ID</span><strong id=\"invRightMeasuresId\">0x00000000</strong></div><div class=\"mini-kpi\"><span>States ID</span><strong id=\"invRightStatesId\">0x00000000</strong></div></div></div></div></div><div class=\"dash-panel-off hidden\" id=\"invDashOff\"><div class=\"dash-panel-off-card\"><h2>Inverters Dashboard Off</h2><p>Para habilitar o dashboard Inverters, desabilite o CAN Monitor.</p></div></div></div></section><section class=\"view\" id=\"canmonitorView\"><div class=\"serial-layout\"><div class=\"card serial-toolbar\"><div class=\"serial-status\"><span id=\"canMonitorState\">CAN monitor offline</span><span id=\"canMonitorCount\">Buffer: 0</span><span id=\"canMonitorPending\">Pending: 0</span><span id=\"canMonitorMode\">Dashboard CAN Monitor OFF</span><span id=\"canRxCount\">RX: 0</span><span id=\"canTxCount\">TX: 0</span><span id=\"canFilterState\">Filter: all</span></div><div class=\"serial-controls\"><button id=\"canPauseViewButton\">Pause View</button><button id=\"canResumeViewButton\">Resume View</button><button id=\"canClearViewButton\">Clear</button><button id=\"canExportButton\">Export Log</button><button id=\"canMonitorToggleButton\">Enable CAN Monitor</button><label class=\"toggle mini\">Autoscroll<input id=\"canAutoscrollToggle\" type=\"checkbox\" checked></label><label class=\"toggle mini\">Wrap<input id=\"canWrapToggle\" type=\"checkbox\" checked></label><label class=\"search\">Direction<select id=\"canDirectionFilter\"><option value=\"all\">all</option><option value=\"rx\">rx</option><option value=\"tx\">tx</option></select></label><label class=\"search\">ID Filter<input id=\"canIdFilterInput\" type=\"text\" placeholder=\"0x123\"></label><label class=\"search\">Search<input id=\"canSearchInput\" type=\"text\" placeholder=\"Find CAN frame\"></label></div></div><div class=\"card serial-card can-monitor-shell\" id=\"canMonitorShell\"><div class=\"can-monitor-off hidden\" id=\"canMonitorOffState\"><div class=\"can-monitor-off-card\"><h2>CAN Monitor Off</h2><p>Ative o monitor CAN bruto para acompanhar frames fora dos dashboards VCU e Inverters.</p><button id=\"canMonitorEnableOverlayButton\">Enable CAN Monitor</button></div></div><div id=\"canStream\" class=\"serial-stream wrap can-stream\"><div class=\"serial-empty\">No CAN frames yet.</div></div></div></div></section>"
"<section class=\"view\" id=\"serialView\"><div class=\"serial-layout\"><div class=\"card serial-toolbar\"><div class=\"serial-status\"><span id=\"serialMonitorState\">Serial offline</span><span id=\"serialMonitorPort\">Port: -</span><span id=\"serialMonitorBaud\">Baud: 0</span><span id=\"serialMonitorCount\">Buffer: 0</span><span id=\"serialMonitorPending\">Pending: 0</span></div><div class=\"serial-controls\"><button id=\"serialPauseViewButton\">Pause View</button><button id=\"serialResumeViewButton\">Resume View</button><button id=\"serialClearViewButton\">Clear</button><button id=\"serialExportButton\">Export Log</button><label class=\"toggle mini\">Autoscroll<input id=\"serialAutoscrollToggle\" type=\"checkbox\" checked></label><label class=\"toggle mini\">Wrap<input id=\"serialWrapToggle\" type=\"checkbox\" checked></label><label class=\"search\">Search<input id=\"serialSearchInput\" type=\"text\" placeholder=\"Find text\"></label></div></div><div class=\"card serial-card\"><div id=\"serialStream\" class=\"serial-stream wrap\"><div class=\"serial-empty\">No serial data yet.</div></div></div></div></section>"
"<section class=\"view\" id=\"settingsView\"><div class=\"settings settings-four\">"
"<div class=\"card\"><h2>Serial</h2><label>COM Port<input id=\"serialComInput\" list=\"comPortsList\" type=\"text\" placeholder=\"COM3\"></label><label>Baud Rate<input id=\"serialBaudInput\" type=\"number\" placeholder=\"115200\"></label><label>Data Bits<input id=\"serialDataBitsInput\" type=\"number\" min=\"5\" max=\"8\" placeholder=\"8\"></label><label>Parity<select id=\"serialParityInput\"><option value=\"none\">none</option><option value=\"odd\">odd</option><option value=\"even\">even</option><option value=\"mark\">mark</option><option value=\"space\">space</option></select></label><label>Stop Bits<select id=\"serialStopBitsInput\"><option value=\"1\">1</option><option value=\"1.5\">1.5</option><option value=\"2\">2</option></select></label><label>Flow Control<select id=\"serialFlowInput\"><option value=\"none\">none</option><option value=\"xonxoff\">xonxoff</option><option value=\"rtscts\">rtscts</option><option value=\"dsrdtr\">dsrdtr</option></select></label><label>Log Path<input id=\"serialLogPathInput\" type=\"text\" placeholder=\"bmt_serial.log\"></label><label class=\"toggle\">Timestamp<input id=\"serialTimestampToggle\" type=\"checkbox\"></label><label class=\"toggle\">Log File<input id=\"serialLogToggle\" type=\"checkbox\"></label><label class=\"toggle\">Reconnect<input id=\"serialReconnectToggle\" type=\"checkbox\"></label><div class=\"buttons\"><button id=\"refreshComPortsButton\">Refresh COMs</button><button id=\"applySerialButton\">Apply Serial</button><button data-action=\"serial_start\">Start</button><button data-action=\"serial_stop\">Stop</button></div><div class=\"kv\"><span>Current Port</span><strong id=\"settingsSerialCom\">-</strong></div><div class=\"kv\"><span>Current Baud</span><strong id=\"settingsSerialBaud\">0</strong></div><div class=\"kv\"><span>Running</span><strong id=\"settingsSerialRunning\">NO</strong></div></div>"
"<div class=\"card\"><h2>CAN Interface</h2><label>COM Port<input id=\"canComInput\" list=\"comPortsList\" type=\"text\" placeholder=\"COM3\"></label><label>Bitrate<input id=\"canBitrateInput\" type=\"number\" placeholder=\"500000\"></label><div class=\"buttons\"><button id=\"applyCanButton\">Apply CAN</button><button data-action=\"can_open\">Open</button><button data-action=\"can_start\">Start</button><button data-action=\"can_stop\">Stop</button><button data-action=\"can_close\">Close</button></div><div class=\"kv\"><span>Current Port</span><strong id=\"settingsCurrentCom\">-</strong></div><div class=\"kv\"><span>Current Bitrate</span><strong id=\"settingsCurrentBitrate\">0</strong></div><div class=\"kv\"><span>UI Port</span><strong id=\"settingsUiPort\">8080</strong></div><div class=\"kv\"><span>COM Detectadas</span><strong id=\"settingsComPorts\">-</strong></div><div class=\"kv\"><span>Ultima Varredura</span><strong id=\"settingsComScan\">-</strong></div></div>"
"<div class=\"card\"><h2>DBC</h2><label>DBC Path<input id=\"dbcPathInput\" type=\"text\" placeholder=\"dbc\\vcu_status.dbc\"></label><div class=\"buttons\"><button id=\"loadDbcButton\">Load DBC</button><button data-action=\"dbc_unload\">Unload</button></div><div class=\"kv\"><span>Loaded</span><strong id=\"settingsDbcLoaded\">NO</strong></div><div class=\"kv\"><span>Version</span><strong id=\"settingsDbcVersion\">-</strong></div><div class=\"kv\"><span>Messages</span><strong id=\"settingsDbcMessages\">0</strong></div><div class=\"kv\"><span>Signals</span><strong id=\"settingsDbcSignals\">0</strong></div></div>"
"<div class=\"card\"><h2>Persistence</h2><label class=\"toggle\">Autosave<input id=\"autosaveToggle\" type=\"checkbox\"></label><div class=\"buttons\"><button data-action=\"config_save\">Save Config</button></div><p class=\"hint\">CLI continua ativa para comandos avancados como bmt -reload e bmt -reset.</p><div class=\"feedback\" id=\"settingsFeedback\">Ready.</div></div>"
"</div></section>"
"<datalist id=\"comPortsList\"></datalist><div class=\"modal hidden\" id=\"aboutBackdrop\"><div class=\"modal-card\"><div class=\"modal-head\"><h2>About This Dashboard</h2><button id=\"aboutClose\">Close</button></div><p>Dashboard: mostra o estado vivo da VCU decodificado pelo DBC.</p><p>Settings: permite configurar COM, bitrate, carregar DBC e controlar o CAN sem decorar comandos.</p><p>O terminal de linha continua sendo a interface principal.</p></div></div>"
"</div><script src=\"/app.js\"></script></body></html>";

static const char *WEB_STYLES_CSS =
":root{--good:#86efb2;--warn:#ffd36a;--bad:#ff8b8b}body{margin:0;font-family:Segoe UI,Arial,sans-serif;background:#081018;color:#edf4ff}*{box-sizing:border-box}"
".splash{position:fixed;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;background:radial-gradient(circle at top,#102037 0%,#071018 60%,#050b12 100%);z-index:9;transition:opacity .7s ease,visibility .7s ease}.splash.hidden{opacity:0;visibility:hidden}.box{position:relative;width:180px;height:180px;border-radius:28px;background:linear-gradient(180deg,#0f2033,#0a1522);border:1px solid rgba(120,180,255,.18);display:flex;align-items:center;justify-content:center}.ring{position:absolute;inset:14px;border-radius:24px;border:1px solid rgba(124,196,255,.25);animation:pulse 1.3s ease-in-out infinite}.core{width:88px;height:88px;border-radius:22px;background:linear-gradient(135deg,#224e81,#12253b);display:flex;align-items:center;justify-content:center;font-weight:800;letter-spacing:.18em}.splash h1{margin:22px 0 8px}"
".app{max-width:1440px;margin:0 auto;padding:24px;opacity:0;transform:translateY(16px);transition:opacity .7s ease,transform .7s ease}.app.ready{opacity:1;transform:translateY(0)}header{display:flex;justify-content:space-between;gap:18px;align-items:flex-start}header h1{margin:8px 0 6px;font-size:40px}.eyebrow{margin:0;color:#6eb8ff;text-transform:uppercase;letter-spacing:.18em;font-size:12px}.sub{margin:0;color:#91a3b7;max-width:720px}.meta{display:flex;gap:10px;flex-wrap:wrap}.meta button,.meta span,nav button,.buttons button,#aboutClose{padding:10px 14px;border-radius:999px;border:1px solid rgba(255,255,255,.08);background:#13263a;color:#d9ebfd;font-weight:700;cursor:pointer}.meta span{cursor:default;background:#18324e}"
"nav{display:flex;gap:10px;margin:18px 0}.tab.active,.dash-tab.active{background:linear-gradient(135deg,#234367,#15283c)}.dash-tab.disabled{opacity:.38;pointer-events:none}.view{display:none}.view.active,.dash-panel.active{display:block}.dashboard-nav{display:flex;gap:10px;margin-bottom:16px}.dash-tab{padding:10px 14px;border-radius:999px;border:1px solid rgba(255,255,255,.08);background:#13263a;color:#d9ebfd;font-weight:700;cursor:pointer}.dash-panel{display:none;position:relative}.kpis{display:grid;grid-template-columns:2fr 2fr 1.4fr;gap:16px;margin-bottom:16px}.grid{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:16px}.card{background:linear-gradient(180deg,#101c28,#152334);border:1px solid rgba(255,255,255,.08);border-radius:18px;padding:18px}.grid>.card{grid-column:span 4}.grid>.wide{grid-column:span 8}.card h2{margin:0 0 14px}.kpis .card strong{display:block;font-size:30px;margin-top:8px}.kpis .card small{color:#91a3b7}.bar{height:12px;border-radius:999px;background:#223041;overflow:hidden;border:1px solid rgba(255,255,255,.08);margin-top:12px}.bar.small{margin:0 0 14px}.bar div{height:100%;width:0;background:linear-gradient(90deg,#58d98f,#b8ffcf)}.row,.kv{display:flex;justify-content:space-between;gap:12px;padding:10px 0;border-bottom:1px solid rgba(255,255,255,.08)}.row:last-child,.kv:last-child{border-bottom:0}.row b,.kv span{color:#91a3b7;font-weight:500}.flags{display:flex;gap:10px;flex-wrap:wrap}.flag{padding:10px 12px;border-radius:12px;background:#213345;color:#93a8bf;font-weight:700;min-width:110px;text-align:center}.flag.on{background:rgba(79,209,139,.16);color:#82f0b2}.flag.alert{background:rgba(255,107,107,.14);color:#ff9a9a}.faults{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:0 18px}.inverter-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.inverter-card{display:grid;gap:16px}.inverter-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start}.meta-stack{display:flex;flex-direction:column;gap:8px;align-items:flex-end}.meta-stack span{padding:8px 12px;border-radius:999px;background:#16324e;border:1px solid rgba(255,255,255,.08)}.kpi-pair{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px}.kpi-pair.compact{grid-template-columns:repeat(4,minmax(0,1fr))}.mini-kpi{padding:14px;border-radius:16px;background:#0e1a27;border:1px solid rgba(255,255,255,.08)}.mini-kpi span{display:block;color:#91a3b7;margin-bottom:8px}.mini-kpi strong{font-size:22px}.dual-kv{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:0 18px}.inverter-flags .flag{min-width:96px}.hidden{display:none !important}.dash-overlay-shell{position:relative}.dash-overlay-shell.off{filter:grayscale(.35) brightness(.48);pointer-events:none}.dash-panel-off{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;background:rgba(3,8,14,.58);backdrop-filter:blur(2px);z-index:2}.dash-panel-off-card{max-width:460px;padding:28px;border-radius:18px;background:linear-gradient(180deg,#101c28,#172435);border:1px solid rgba(255,255,255,.1);text-align:center}.dash-panel-off-card h2{margin:0 0 12px}.dash-panel-off-card p{margin:0;color:#a7b7ca;line-height:1.5}.can-monitor-shell{position:relative}.can-monitor-shell.off .serial-stream{filter:grayscale(.35) brightness(.48);pointer-events:none}.can-monitor-off{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;background:rgba(3,8,14,.58);backdrop-filter:blur(2px);z-index:2}.can-monitor-off-card{max-width:440px;padding:28px;border-radius:18px;background:linear-gradient(180deg,#101c28,#172435);border:1px solid rgba(255,255,255,.1);text-align:center}.can-monitor-off-card h2{margin:0 0 12px}.can-monitor-off-card p{margin:0 0 18px;color:#a7b7ca;line-height:1.5}.can-monitor-off-card button{padding:12px 18px;border-radius:999px;border:1px solid rgba(255,255,255,.08);background:#234367;color:#edf4ff;font-weight:700;cursor:pointer}"
".settings{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px}.settings-four{grid-template-columns:repeat(4,minmax(0,1fr))}.settings label{display:flex;flex-direction:column;gap:8px;color:#91a3b7;margin-bottom:12px}.settings input,.settings select,.serial-controls input,.serial-controls select{padding:12px 14px;border-radius:12px;border:1px solid rgba(255,255,255,.08);background:#0d1824;color:#edf4ff}.buttons{display:flex;gap:10px;flex-wrap:wrap;margin:8px 0 12px}.toggle{display:flex;flex-direction:row !important;align-items:center;justify-content:space-between}.toggle input{width:20px;height:20px}.toggle.mini{gap:10px;margin:0;color:#c4d6ea}.toggle.mini input{width:18px;height:18px}.search{display:flex;flex-direction:column;gap:8px;color:#91a3b7;min-width:170px}.hint{color:#91a3b7;line-height:1.45}.feedback{margin-top:14px;padding:12px 14px;border-radius:14px;background:#0f1f30;border:1px solid rgba(255,255,255,.08)}.feedback.good{color:#86efb2;border-color:rgba(134,239,178,.35)}.feedback.bad{color:#ff8b8b;border-color:rgba(255,139,139,.35)}.serial-layout{display:grid;gap:16px}.serial-toolbar{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;flex-wrap:wrap}.serial-status,.serial-controls{display:flex;gap:10px;flex-wrap:wrap;align-items:center}.serial-status span{padding:10px 14px;border-radius:999px;background:#13263a;border:1px solid rgba(255,255,255,.08);color:#d9ebfd}.serial-card{padding:0;overflow:hidden}.serial-stream{height:62vh;overflow:auto;padding:18px;background:radial-gradient(circle at top,#0b1521 0%,#09111a 55%,#070d14 100%);font-family:Consolas,'Courier New',monospace;font-size:14px;line-height:1.5;white-space:pre}.serial-stream.wrap{white-space:pre-wrap;word-break:break-word}.serial-line{display:grid;grid-template-columns:110px 74px 1fr;gap:12px;padding:4px 0;border-bottom:1px solid rgba(255,255,255,.04)}.serial-line.can-line{grid-template-columns:110px 64px 64px 116px 1fr;align-items:start}.serial-line.hidden{display:none}.serial-time{color:#7fa7c9}.serial-tag{font-weight:700;color:#9fb8d2}.serial-tag.info{color:#77d8ff}.serial-tag.warning{color:#ffd36a}.serial-tag.error{color:#ff8b8b}.serial-tag.can-rx{color:#86efb2}.serial-tag.can-tx{color:#7dd9ff}.serial-type{font-weight:700;color:#d0d9e7}.serial-type.std{color:#a9c7ff}.serial-type.ext{color:#ffcf7a}.serial-type.rtr,.serial-type.xtr{color:#ff9da1}.serial-id{font-weight:700;color:#d9ebfd}.serial-text{color:#edf4ff}.can-stream .serial-text{color:#dfeaff}.serial-empty{color:#7d95ab;padding:18px 0;text-align:center}"
".settings{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px}.settings-four{grid-template-columns:repeat(4,minmax(0,1fr))}.settings label{display:flex;flex-direction:column;gap:8px;color:#91a3b7;margin-bottom:12px}.settings input,.settings select,.serial-controls input,.serial-controls select{padding:12px 14px;border-radius:12px;border:1px solid rgba(255,255,255,.08);background:#0d1824;color:#edf4ff}.buttons{display:flex;gap:10px;flex-wrap:wrap;margin:8px 0 12px}.toggle{display:flex;flex-direction:row !important;align-items:center;justify-content:space-between}.toggle input{width:20px;height:20px}.toggle.mini{gap:10px;margin:0;color:#c4d6ea}.toggle.mini input{width:18px;height:18px}.search{display:flex;flex-direction:column;gap:8px;color:#91a3b7;min-width:170px}.hint{color:#91a3b7;line-height:1.45}.feedback{margin-top:14px;padding:12px 14px;border-radius:14px;background:#0f1f30;border:1px solid rgba(255,255,255,.08)}.feedback.good{color:#86efb2;border-color:rgba(134,239,178,.35)}.feedback.bad{color:#ff8b8b;border-color:rgba(255,139,139,.35)}.serial-layout{display:grid;gap:16px}.serial-toolbar{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;flex-wrap:wrap}.serial-status,.serial-controls{display:flex;gap:10px;flex-wrap:wrap;align-items:center}.serial-status span{padding:10px 14px;border-radius:999px;background:#13263a;border:1px solid rgba(255,255,255,.08);color:#d9ebfd}.serial-card{padding:0;overflow:hidden}.serial-stream{height:62vh;overflow:auto;padding:18px;background:radial-gradient(circle at top,#0b1521 0%,#09111a 55%,#070d14 100%);font-family:Consolas,'Courier New',monospace;font-size:14px;line-height:1.5;white-space:pre}.serial-stream.wrap{white-space:pre-wrap;word-break:break-word}.serial-line{display:grid;grid-template-columns:110px 74px 1fr;gap:12px;padding:4px 0;border-bottom:1px solid rgba(255,255,255,.04)}.serial-line.can-line{grid-template-columns:110px 64px 64px 116px 1fr;align-items:start}.serial-line.hidden{display:none}.serial-time{color:#7fa7c9}.serial-tag{font-weight:700;color:#9fb8d2}.serial-tag.info{color:#77d8ff}.serial-tag.warning{color:#ffd36a}.serial-tag.error{color:#ff8b8b}.serial-tag.can-rx{color:#86efb2}.serial-tag.can-tx{color:#7dd9ff}.serial-type{font-weight:700;color:#d0d9e7}.serial-type.std{color:#a9c7ff}.serial-type.ext{color:#ffcf7a}.serial-type.rtr,.serial-type.xtr{color:#ff9da1}.serial-id{font-weight:700;color:#d9ebfd}.serial-text{color:#edf4ff}.can-stream .serial-text{color:#dfeaff}.serial-empty{color:#7d95ab;padding:18px 0;text-align:center}.quick-head{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;flex-wrap:wrap;margin-bottom:16px}.quick-status,.quick-actions{display:flex;gap:10px;flex-wrap:wrap;align-items:center}.quick-status span{padding:10px 14px;border-radius:999px;background:#13263a;border:1px solid rgba(255,255,255,.08);color:#d9ebfd}.quick-actions button{padding:10px 14px;border-radius:999px;border:1px solid rgba(255,255,255,.08);background:#13263a;color:#d9ebfd;font-weight:700;cursor:pointer}"
".value-good{color:var(--good)}.value-warn{color:var(--warn)}.value-bad{color:var(--bad)}.value-muted{color:#cfdced}.modal{position:fixed;inset:0;background:rgba(4,10,16,.72);display:flex;align-items:center;justify-content:center;padding:24px}.modal.hidden{display:none}.modal-card{width:min(720px,100%);background:linear-gradient(180deg,#102033,#0b1521);border:1px solid rgba(255,255,255,.08);border-radius:22px;padding:22px}.modal-head{display:flex;justify-content:space-between;gap:12px;align-items:center}@keyframes pulse{0%{transform:scale(.92)}50%{transform:scale(1)}100%{transform:scale(.92)}}@media(max-width:1200px){.kpis,.settings,.grid,.faults{grid-template-columns:1fr}.grid>.card,.grid>.wide{grid-column:span 1}}@media(max-width:780px){header{flex-direction:column}.kpis{grid-template-columns:1fr}}";

static const char *WEB_APP_JS =
"const aboutBackdrop=document.getElementById('aboutBackdrop');const splash=document.getElementById('splash');const app=document.getElementById('app');const feedback=document.getElementById('settingsFeedback');const serialStream=document.getElementById('serialStream');const canStream=document.getElementById('canStream');let lastSeen='';let lastAt=0;let invLeftSeen='';let invLeftAt=0;let invRightSeen='';let invRightAt=0;let serialLastId=0;let serialPaused=false;let serialPending=[];let canLastId=0;let canPaused=false;let canPending=[];let canRxTotal=0;let canTxTotal=0;"
"const setText=(id,v)=>{const el=document.getElementById(id);if(el)el.textContent=v;};const setState=(id,text,cls)=>{const el=document.getElementById(id);if(!el)return;el.textContent=text;el.classList.remove('value-good','value-warn','value-bad','value-muted');if(cls)el.classList.add(cls);};const boolText=v=>v?'YES':'NO';const boolClass=(v,i=false)=>v?(i?'value-bad':'value-good'):(i?'value-good':'value-muted');const toClass=h=>h==='error'?'error':(h==='warning'?'warning':(h==='info'?'info':''));"
"const setFlag=(id,v,a=false)=>{const el=document.getElementById(id);if(!el)return;el.classList.toggle('on',!!v&&!a);el.classList.toggle('alert',!!v&&a);el.textContent=el.textContent.split(' ')[0]+(v?' ON':'');};const showFeedback=(m,ok=true)=>{feedback.textContent=m;feedback.classList.remove('good','bad');feedback.classList.add(ok?'good':'bad');};const postJson=async(u,p)=>{const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p||{})});return r.json();};"
"const switchView=v=>{document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('active',b.dataset.view===v));document.querySelectorAll('.view').forEach(p=>p.classList.toggle('active',p.id===v+'View'));};const switchDash=v=>{document.querySelectorAll('.dash-tab').forEach(b=>b.classList.toggle('active',b.dataset.dash===v));document.querySelectorAll('.dash-panel').forEach(p=>p.classList.toggle('active',p.id===v+'Panel'));};const applyCanMonitorMode=enabled=>{const shell=document.getElementById('canMonitorShell');const overlay=document.getElementById('canMonitorOffState');const toggleButton=document.getElementById('canMonitorToggleButton');const overlayButton=document.getElementById('canMonitorEnableOverlayButton');const vcuShell=document.getElementById('vcuOverlayShell');const vcuOff=document.getElementById('vcuDashOff');const invShell=document.getElementById('invertersOverlayShell');const invOff=document.getElementById('invDashOff');setText('canMonitorMode',enabled?'Dashboard CAN Monitor ON':'Dashboard CAN Monitor OFF');shell.classList.toggle('off',!enabled);overlay.classList.toggle('hidden',enabled);if(vcuShell&&vcuOff){vcuShell.classList.toggle('off',enabled);vcuOff.classList.toggle('hidden',!enabled);}if(invShell&&invOff){invShell.classList.toggle('off',enabled);invOff.classList.toggle('hidden',!enabled);}toggleButton.textContent=enabled?'Disable CAN Monitor':'Enable CAN Monitor';overlayButton.textContent='Enable CAN Monitor';};document.querySelectorAll('.tab').forEach(b=>b.addEventListener('click',()=>switchView(b.dataset.view)));document.querySelectorAll('.dash-tab').forEach(b=>b.addEventListener('click',()=>{if(!b.classList.contains('disabled'))switchDash(b.dataset.dash);}));document.getElementById('aboutButton').addEventListener('click',()=>aboutBackdrop.classList.remove('hidden'));document.getElementById('aboutClose').addEventListener('click',()=>aboutBackdrop.classList.add('hidden'));aboutBackdrop.addEventListener('click',e=>{if(e.target===aboutBackdrop)aboutBackdrop.classList.add('hidden');});window.addEventListener('load',()=>{setTimeout(()=>splash.classList.add('hidden'),2800);setTimeout(()=>app.classList.add('ready'),3050);});"
"function applySerialFilter(){const q=(document.getElementById('serialSearchInput').value||'').trim().toLowerCase();serialStream.querySelectorAll('.serial-line').forEach(line=>{const text=(line.dataset.search||'');line.classList.toggle('hidden',q!==''&&!text.includes(q));});}function updatePendingLabel(){setText('serialMonitorPending',`Pending: ${serialPending.length}`);}function scrollSerialToEnd(){if(document.getElementById('serialAutoscrollToggle').checked){serialStream.scrollTop=serialStream.scrollHeight;}}"
"function appendSerialEntries(entries){entries.forEach(entry=>{const row=document.createElement('div');const tagClass=toClass(entry.highlight);row.className='serial-line';row.dataset.search=(entry.text||'').toLowerCase();row.innerHTML=`<span class=\"serial-time\">${entry.timestamp}</span><span class=\"serial-tag ${tagClass}\">${entry.highlight.toUpperCase()}</span><span class=\"serial-text\"></span>`;row.querySelector('.serial-text').textContent=entry.text||'';serialStream.appendChild(row);});while(serialStream.children.length>2200){serialStream.removeChild(serialStream.firstChild);}if(serialStream.children.length===0){serialStream.innerHTML='<div class=\"serial-empty\">No serial data yet.</div>';return;}const empty=serialStream.querySelector('.serial-empty');if(empty)empty.remove();applySerialFilter();scrollSerialToEnd();}"
"function parseCanMeta(text){const raw=text||'';const direction=(raw.match(/\\[(RX|TX)\\]/i)||raw.match(/\\b(RX|TX)\\b/))?.[1]?.toUpperCase()||'--';const id=(raw.match(/ID:(0x[0-9A-F]+)/i)||raw.match(/\\b(0x[0-9A-F]{3,8})\\b/i))?.[1]?.toUpperCase()||'0x---';const type=(raw.match(/TYPE:([A-Z]+)/i)||raw.match(/\\b(STD|EXT|RTR|XTR)\\b/))?.[1]?.toUpperCase()||'RAW';return{direction,id,type};}"
"function applyCanFilter(){const q=(document.getElementById('canSearchInput').value||'').trim().toLowerCase();const direction=(document.getElementById('canDirectionFilter').value||'all').toLowerCase();const idFilter=(document.getElementById('canIdFilterInput').value||'').trim().toLowerCase();canStream.querySelectorAll('.serial-line').forEach(line=>{const text=(line.dataset.search||'');const dir=(line.dataset.direction||'').toLowerCase();const canId=(line.dataset.canId||'').toLowerCase();const matchesSearch=(q===''||text.includes(q));const matchesDirection=(direction==='all'||dir===direction);const matchesId=(idFilter===''||canId.includes(idFilter));line.classList.toggle('hidden',!(matchesSearch&&matchesDirection&&matchesId));});setText('canFilterState',`Filter: ${direction}${idFilter?` / ${idFilter}`:''}${q?` / ${q}`:''}`);}function updateCanPendingLabel(){setText('canMonitorPending',`Pending: ${canPending.length}`);}function scrollCanToEnd(){if(document.getElementById('canAutoscrollToggle').checked){canStream.scrollTop=canStream.scrollHeight;}}function nowText(){const d=new Date();return `${String(d.getHours()).padStart(2,'0')}:${String(d.getMinutes()).padStart(2,'0')}:${String(d.getSeconds()).padStart(2,'0')}`;}async function refreshComPorts(showToast=false){try{const resp=await fetch('/api/com-ports');const data=await resp.json();const list=document.getElementById('comPortsList');const ports=Array.isArray(data.ports)?data.ports:[];list.innerHTML=ports.map(p=>`<option value=\"${p}\"></option>`).join('');setText('settingsComPorts',ports.length?ports.join(', '):'-');setText('settingsComScan',nowText());if(showToast){showFeedback(ports.length?`${ports.length} porta(s) COM detectada(s).`:'Nenhuma porta COM detectada.',ports.length>0);}}catch(e){setText('settingsComPorts','-');setText('settingsComScan','falhou');if(showToast)showFeedback('Falha ao atualizar a lista de portas COM.',false);}}function triggerDownload(url){const link=document.createElement('a');link.href=url;link.download='';document.body.appendChild(link);link.click();document.body.removeChild(link);}function freshnessClass(age){if(age===null)return'value-muted';if(age>1500)return'value-warn';return'value-good';}"
"function appendCanEntries(entries){entries.forEach(entry=>{const row=document.createElement('div');const meta=parseCanMeta(entry.text||'');const dirClass=meta.direction==='RX'?'can-rx':(meta.direction==='TX'?'can-tx':toClass(entry.highlight));row.className='serial-line can-line';row.dataset.search=(entry.text||'').toLowerCase();row.dataset.direction=(meta.direction||'').toLowerCase();row.dataset.canId=(meta.id||'').toLowerCase();row.dataset.canType=(meta.type||'').toLowerCase();row.innerHTML=`<span class=\"serial-time\">${entry.timestamp}</span><span class=\"serial-tag ${dirClass}\">${meta.direction}</span><span class=\"serial-type ${meta.type.toLowerCase()}\">${meta.type}</span><span class=\"serial-id\">${meta.id}</span><span class=\"serial-text\"></span>`;row.querySelector('.serial-text').textContent=entry.text||'';if(meta.direction==='RX')canRxTotal++;else if(meta.direction==='TX')canTxTotal++;canStream.appendChild(row);});setText('canRxCount',`RX: ${canRxTotal}`);setText('canTxCount',`TX: ${canTxTotal}`);while(canStream.children.length>2200){canStream.removeChild(canStream.firstChild);}if(canStream.children.length===0){canStream.innerHTML='<div class=\"serial-empty\">No CAN frames yet.</div>';return;}const empty=canStream.querySelector('.serial-empty');if(empty)empty.remove();applyCanFilter();scrollCanToEnd();}"
"async function refreshSerialStatus(){try{const resp=await fetch('/api/serial/status');const status=await resp.json();setText('serialMonitorState',status.running?'Serial running':'Serial offline');setText('serialMonitorPort',`Port: ${status.com_port||'-'}`);setText('serialMonitorBaud',`Baud: ${status.baud_rate||0}`);setText('serialMonitorCount',`Buffer: ${status.buffer_count||0}`);}catch(e){setText('serialMonitorState','Serial monitor unavailable');}}"
"async function refreshSerialLogs(){try{const resp=await fetch('/api/serial/logs?after='+serialLastId);const data=await resp.json();if(Array.isArray(data.entries)&&data.entries.length>0){serialLastId=data.next_after||serialLastId;if(serialPaused){serialPending.push(...data.entries);updatePendingLabel();}else{appendSerialEntries(data.entries);}}}catch(e){}}"
"async function refreshCanStatus(){try{const resp=await fetch('/api/can-monitor/status');const status=await resp.json();setText('canMonitorState',status.running?'CAN running':'CAN offline');setText('canMonitorCount',`Buffer: ${status.buffer_count||0}`);applyCanMonitorMode(!!status.enabled);}catch(e){setText('canMonitorState','CAN monitor unavailable');}}"
"async function refreshCanLogs(){try{const resp=await fetch('/api/can-monitor/logs?after='+canLastId);const data=await resp.json();if(Array.isArray(data.entries)&&data.entries.length>0){canLastId=data.next_after||canLastId;if(canPaused){canPending.push(...data.entries);updateCanPendingLabel();}else{appendCanEntries(data.entries);}}}catch(e){}}"
"document.getElementById('serialPauseViewButton').addEventListener('click',()=>{serialPaused=true;setText('serialMonitorState','Serial paused');});document.getElementById('serialResumeViewButton').addEventListener('click',()=>{serialPaused=false;if(serialPending.length>0){appendSerialEntries(serialPending);serialPending=[];updatePendingLabel();}refreshSerialStatus();});document.getElementById('serialClearViewButton').addEventListener('click',async()=>{try{const r=await postJson('/api/settings/action',{action:'serial_log_clear'});showFeedback(r.message,r.ok);serialStream.innerHTML='<div class=\"serial-empty\">No serial data yet.</div>';serialLastId=0;serialPending=[];updatePendingLabel();await refreshSerialStatus();}catch(e){showFeedback('Falha ao limpar o monitor serial.',false);}});document.getElementById('serialExportButton').addEventListener('click',()=>triggerDownload('/api/serial/export'));document.getElementById('serialSearchInput').addEventListener('input',applySerialFilter);document.getElementById('serialWrapToggle').addEventListener('change',e=>serialStream.classList.toggle('wrap',e.target.checked));document.getElementById('canPauseViewButton').addEventListener('click',()=>{canPaused=true;setText('canMonitorState','CAN paused');});document.getElementById('canResumeViewButton').addEventListener('click',()=>{canPaused=false;if(canPending.length>0){appendCanEntries(canPending);canPending=[];updateCanPendingLabel();}refreshCanStatus();});document.getElementById('canClearViewButton').addEventListener('click',async()=>{try{const r=await postJson('/api/settings/action',{action:'can_log_clear'});showFeedback(r.message,r.ok);canStream.innerHTML='<div class=\"serial-empty\">No CAN frames yet.</div>';canLastId=0;canPending=[];canRxTotal=0;canTxTotal=0;setText('canRxCount','RX: 0');setText('canTxCount','TX: 0');updateCanPendingLabel();await refreshCanStatus();}catch(e){showFeedback('Falha ao limpar o monitor CAN.',false);}});document.getElementById('canExportButton').addEventListener('click',()=>triggerDownload('/api/can-monitor/export'));document.getElementById('canSearchInput').addEventListener('input',applyCanFilter);document.getElementById('canIdFilterInput').addEventListener('input',applyCanFilter);document.getElementById('canDirectionFilter').addEventListener('change',applyCanFilter);document.getElementById('canWrapToggle').addEventListener('change',e=>canStream.classList.toggle('wrap',e.target.checked));document.getElementById('canMonitorToggleButton').addEventListener('click',async()=>{try{const enable=document.getElementById('canMonitorToggleButton').textContent.toLowerCase().includes('enable');const r=await postJson('/api/settings/action',{action:enable?'can_monitor_on':'can_monitor_off'});showFeedback(r.message,r.ok);await refreshCanStatus();await refresh();}catch(e){showFeedback('Falha ao alternar o CAN Monitor.',false);}});document.getElementById('canMonitorEnableOverlayButton').addEventListener('click',async()=>{try{const r=await postJson('/api/settings/action',{action:'can_monitor_on'});showFeedback(r.message,r.ok);await refreshCanStatus();await refresh();}catch(e){showFeedback('Falha ao ativar o CAN Monitor.',false);}});"
"document.getElementById('applySerialButton').addEventListener('click',async()=>{try{const r=await postJson('/api/settings/serial',{com_port:document.getElementById('serialComInput').value.trim(),baud_rate:Number(document.getElementById('serialBaudInput').value||0),data_bits:Number(document.getElementById('serialDataBitsInput').value||0),parity:document.getElementById('serialParityInput').value.trim(),stop_bits:document.getElementById('serialStopBitsInput').value.trim(),flow_control:document.getElementById('serialFlowInput').value.trim(),timestamp_enabled:document.getElementById('serialTimestampToggle').checked,logfile_enabled:document.getElementById('serialLogToggle').checked,logfile_path:document.getElementById('serialLogPathInput').value.trim(),reconnect_enabled:document.getElementById('serialReconnectToggle').checked});showFeedback(r.message,r.ok);await refresh();await refreshSerialStatus();}catch(e){showFeedback('Falha ao aplicar configuracoes seriais.',false);}});"
"document.querySelectorAll('[data-action]').forEach(b=>b.addEventListener('click',async()=>{try{const r=await postJson('/api/settings/action',{action:b.dataset.action});showFeedback(r.message,r.ok);await refresh();await refreshSerialStatus();}catch(e){showFeedback('Falha ao executar acao.',false);}}));document.querySelectorAll('.quick-nav-settings').forEach(b=>b.addEventListener('click',()=>switchView('settings')));document.getElementById('refreshComPortsButton').addEventListener('click',()=>refreshComPorts(true));document.getElementById('applyCanButton').addEventListener('click',async()=>{try{const r=await postJson('/api/settings/can',{com_port:document.getElementById('canComInput').value.trim(),bitrate:Number(document.getElementById('canBitrateInput').value||0)});showFeedback(r.message,r.ok);await refresh();}catch(e){showFeedback('Falha ao aplicar configuracoes CAN.',false);}});document.getElementById('loadDbcButton').addEventListener('click',async()=>{try{const r=await postJson('/api/settings/dbc/load',{path:document.getElementById('dbcPathInput').value.trim()});showFeedback(r.message,r.ok);await refresh();}catch(e){showFeedback('Falha ao carregar o DBC.',false);}});document.getElementById('autosaveToggle').addEventListener('change',async e=>{try{const r=await postJson('/api/settings/action',{action:e.target.checked?'autosave_on':'autosave_off'});showFeedback(r.message,r.ok);await refresh();}catch(err){showFeedback('Falha ao atualizar autosave.',false);}});"
"function hydrateSettings(s){const a=document.getElementById('canComInput');const b=document.getElementById('canBitrateInput');const c=document.getElementById('dbcPathInput');const d=document.getElementById('serialComInput');const e=document.getElementById('serialBaudInput');const f=document.getElementById('serialDataBitsInput');const g=document.getElementById('serialParityInput');const h=document.getElementById('serialStopBitsInput');const i=document.getElementById('serialFlowInput');const j=document.getElementById('serialLogPathInput');if(document.activeElement!==a)a.value=s.com_port||'';if(document.activeElement!==b)b.value=s.bitrate||'';if(document.activeElement!==c)c.value=s.dbc_path||'';if(document.activeElement!==d)d.value=s.serial_com_port||'';if(document.activeElement!==e)e.value=s.serial_baud_rate||'';if(document.activeElement!==f)f.value=s.serial_data_bits||'';if(document.activeElement!==g)g.value=s.serial_parity||'';if(document.activeElement!==h)h.value=s.serial_stop_bits||'';if(document.activeElement!==i)i.value=s.serial_flow_control||'';if(document.activeElement!==j)j.value=s.serial_logfile_path||'';document.getElementById('serialTimestampToggle').checked=!!s.serial_timestamp_enabled;document.getElementById('serialLogToggle').checked=!!s.serial_logfile_enabled;document.getElementById('serialReconnectToggle').checked=!!s.serial_reconnect_enabled;document.getElementById('autosaveToggle').checked=!!s.autosave;setText('settingsSerialCom',s.serial_com_port||'-');setText('settingsSerialBaud',String(s.serial_baud_rate||0));setText('settingsSerialRunning',s.serial_running?'YES':'NO');setText('settingsCurrentCom',s.com_port||'-');setText('settingsCurrentBitrate',String(s.bitrate||0));setText('settingsUiPort',String(s.ui_port||0));setText('settingsDbcLoaded',s.dbc_loaded?'YES':'NO');setText('settingsDbcVersion',s.dbc_version||'-');setText('settingsDbcMessages',String(s.dbc_messages||0));setText('settingsDbcSignals',String(s.dbc_signals||0));applyCanMonitorMode(!!s.can_monitor_enabled);}"
"function hydrateInverterSide(prefix,data){setText(prefix+'State',data.configured?(data.state||'N/A'):'Awaiting Base');setText(prefix+'Online',data.online?'Online':'Offline');setText(prefix+'Update',data.last_update||'No data');setText(prefix+'Speed',Number(data.speed_motor||0).toFixed(0)+' rpm');setText(prefix+'Vdc',Number(data.voltage_dc||0).toFixed(1)+' V');setText(prefix+'Iac',Number(data.current_ac||0).toFixed(1)+' A');setText(prefix+'Tigbt',Number(data.temp_igbt||0).toFixed(1)+' C');setText(prefix+'Mode',data.control_mode||'N/A');setText(prefix+'Direction',data.direction||'N/A');setText(prefix+'SpeedRef',Number(data.speed_ref||0).toFixed(0)+' rpm');setText(prefix+'TorqueRef',Number(data.torque_ref||0).toFixed(1)+' %');setText(prefix+'Vac',Number(data.voltage_ac||0).toFixed(1)+' V');setText(prefix+'Tmotor',Number(data.temp_motor||0).toFixed(1)+' C');setText(prefix+'TmotorAux',Number(data.temp_motor_aux||0).toFixed(1)+' C');setText(prefix+'Event',data.event||'N/A');setText(prefix+'GdReady',String(data.gd_ready_count||0));setText(prefix+'GdFaults',String(data.gd_fault_count||0));setText(prefix+'MeasuresId','0x'+Number(data.measures_id||0).toString(16).toUpperCase().padStart(8,'0'));setText(prefix+'StatesId','0x'+Number(data.states_id||0).toString(16).toUpperCase().padStart(8,'0'));setFlag(prefix+'OnOff',!!data.onoff);setFlag(prefix+'OverSpeed',!!data.over_speed,true);setFlag(prefix+'OverVoltage',!!data.over_voltage,true);setFlag(prefix+'UnderVoltage',!!data.under_voltage,true);setFlag(prefix+'OverTempMotor',!!data.over_temp_motor,true);setFlag(prefix+'OverTempIgbt',!!data.over_temp_igbt,true);setFlag(prefix+'FocError',!!data.foc_error,true);setFlag(prefix+'RunError',!!data.run_error,true);setFlag(prefix+'InitError',!!data.init_error,true);}"
"async function refreshInverters(settings){try{const resp=await fetch('/api/inverters');const inv=await resp.json();const left=inv.left||{};const right=inv.right||{};if(left.last_update&&left.last_update!==invLeftSeen){invLeftSeen=left.last_update;invLeftAt=Date.now();}if(right.last_update&&right.last_update!==invRightSeen){invRightSeen=right.last_update;invRightAt=Date.now();}const leftAge=invLeftAt?Date.now()-invLeftAt:null;const rightAge=invRightAt?Date.now()-invRightAt:null;const leftStale=!!left.configured&&leftAge!==null&&leftAge>1500;const rightStale=!!right.configured&&rightAge!==null&&rightAge>1500;hydrateInverterSide('invLeft',left);hydrateInverterSide('invRight',right);if(leftStale)setState('invLeftOnline','Stale','value-warn');if(rightStale)setState('invRightOnline','Stale','value-warn');setText('invTopCan',document.getElementById('canState').textContent||'CAN offline');setText('invTopDbc',settings&&settings.inverter_dbc_loaded?'DBC Inverter: loaded':'DBC Inverter: off');setText('invTopLeftBase',settings&&settings.inverter_left_base_set?('Left base: 0x'+Number(settings.inverter_left_base||0).toString(16).toUpperCase()):'Left base: --');setText('invTopRightBase',settings&&settings.inverter_right_base_set?('Right base: 0x'+Number(settings.inverter_right_base||0).toString(16).toUpperCase()):'Right base: --');setState('invTopUpdate','Last: '+(left.last_update||right.last_update||'--')+' / '+((leftStale||rightStale)?'STALE':'LIVE'),(leftStale||rightStale)?'value-warn':'value-good');}catch(e){}}"
"async function refresh(){try{const [statusResp,vcuResp,settingsResp]=await Promise.all([fetch('/api/status'),fetch('/api/vcu'),fetch('/api/settings')]);const status=await statusResp.json();const vcu=await vcuResp.json();const settings=await settingsResp.json();if(vcu.last_update&&vcu.last_update!==lastSeen){lastSeen=vcu.last_update;lastAt=Date.now();}const fresh=lastAt?Date.now()-lastAt:null;setText('serverState',status.ui_running?'UI online':'UI offline');setText('canState',status.can_running?'CAN running':(status.can_opened?'CAN open':'CAN offline'));setText('lastUpdate',vcu.last_update||'Sem dados');setText('rx_count',String(status.rx_count));setText('dbc_loaded',status.dbc_loaded?'YES':'NO');setState('freshness',fresh===null?'No data':String(Math.max(0,Math.round(fresh)))+' ms',freshnessClass(fresh));setText('vcuTopCan',status.can_running?'CAN running':(status.can_opened?'CAN open':'CAN offline'));setText('vcuTopDbc',status.dbc_loaded?'DBC VCU: loaded':'DBC VCU: off');setState('vcuTopFresh',fresh===null?'Freshness: no data':(fresh>1500?'Freshness: STALE ':'Freshness: LIVE ')+String(Math.max(0,Math.round(fresh)))+' ms',freshnessClass(fresh));setState('vcuTopUpdate','Last: '+(vcu.last_update||'--'),freshnessClass(fresh));setState('vcu_state',vcu.vcu_state,'value-muted');setState('vcu_state_kpi',vcu.vcu_state,'value-muted');setText('command_source',vcu.command_source);setText('command_source_kpi','Source: '+vcu.command_source);setState('torque_allowed',boolText(vcu.torque_allowed),boolClass(vcu.torque_allowed));setState('torque_limited',boolText(vcu.torque_limited),boolClass(vcu.torque_limited,true));setText('status_version',String(vcu.status_version));setText('alive_counter',String(vcu.alive_counter));setText('longitudinal_request',vcu.longitudinal_request.toFixed(1)+' %');setText('longitudinal_request_kpi',vcu.longitudinal_request.toFixed(1)+' %');document.getElementById('longitudinal_bar').style.width=Math.max(0,Math.min(100,vcu.longitudinal_request))+'%';document.getElementById('longitudinal_bar_small').style.width=Math.max(0,Math.min(100,vcu.longitudinal_request))+'%';setText('throttle_state',vcu.throttle_state);setText('brake_state',vcu.brake_state);setState('pedals_valid',boolText(vcu.pedals_valid),boolClass(vcu.pedals_valid));setState('pedals_tx_enabled',boolText(vcu.pedals_tx_enabled),boolClass(vcu.pedals_tx_enabled));setText('pedals_alive_counter',String(vcu.pedals_alive_counter));setState('throttle_health',vcu.throttle_health,vcu.throttle_health==='CRITICAL'?'value-bad':(vcu.throttle_health==='OK'?'value-good':'value-warn'));setState('brake_health',vcu.brake_health,vcu.brake_health==='CRITICAL'?'value-bad':(vcu.brake_health==='OK'?'value-good':'value-warn'));setState('throttle_raw_health',vcu.throttle_raw_health,vcu.throttle_raw_health==='CRITICAL'?'value-bad':(vcu.throttle_raw_health==='OK'?'value-good':'value-warn'));setState('brake_raw_health',vcu.brake_raw_health,vcu.brake_raw_health==='CRITICAL'?'value-bad':(vcu.brake_raw_health==='OK'?'value-good':'value-warn'));setState('hw_fault',boolText(vcu.hw_fault),boolClass(vcu.hw_fault,true));setState('can_fault',boolText(vcu.can_fault),boolClass(vcu.can_fault,true));setState('inverter_handshake_fault',boolText(vcu.inverter_handshake_fault),boolClass(vcu.inverter_handshake_fault,true));setState('startup_failed',boolText(vcu.startup_failed),boolClass(vcu.startup_failed,true));setState('tcu_heartbeat_ok',boolText(vcu.tcu_heartbeat_ok),boolClass(vcu.tcu_heartbeat_ok));setState('hpc_heartbeat_ok',boolText(vcu.hpc_heartbeat_ok),boolClass(vcu.hpc_heartbeat_ok));setState('inverter_handshake_ok',boolText(vcu.inverter_handshake_ok),boolClass(vcu.inverter_handshake_ok));setState('controlled_reset_req',boolText(vcu.controlled_reset_req),boolClass(vcu.controlled_reset_req,true));setState('brake_dominance_hard',boolText(vcu.brake_dominance_hard),boolClass(vcu.brake_dominance_hard,true));setFlag('regen_enabled',vcu.regen_enabled);setFlag('yaw_enabled',vcu.yaw_enabled);setFlag('traction_enabled',vcu.traction_enabled);setFlag('driver_takeover',vcu.driver_takeover,true);setFlag('remote_authorized',vcu.remote_authorized);hydrateSettings(settings);await refreshInverters(settings);await refreshCanStatus();}catch(e){setText('serverState','Falha de conexao');setText('canState','CAN offline');showFeedback('Falha ao atualizar dados da interface.',false);}}refresh();refreshComPorts();refreshSerialStatus();refreshSerialLogs();refreshInverters({});refreshCanStatus();refreshCanLogs();setInterval(refresh,250);setInterval(refreshComPorts,5000);setInterval(refreshSerialStatus,500);setInterval(refreshSerialLogs,200);setInterval(refreshCanStatus,500);setInterval(refreshCanLogs,200);";

/**
 * @brief Envia uma resposta HTTP textual completa.
 */
static void web_send_response(SOCKET client, const char *content_type, const char *body);
/**
 * @brief Envia uma resposta HTTP baseada em bytes ja montados.
 */
static void web_send_response_bytes(SOCKET client, const char *content_type, const char *body, size_t body_length);
/**
 * @brief Envia uma resposta HTTP para download de arquivo texto.
 */
static void web_send_download_response(SOCKET client, const char *filename, const char *body, size_t body_length);
/**
 * @brief Envia uma resposta HTTP 404 em JSON.
 */
static void web_send_not_found(SOCKET client);
/**
 * @brief Envia um JSON simples indicando sucesso ou falha de uma acao.
 */
static void web_send_json_result(SOCKET client, int ok, const char *message);
/**
 * @brief Monta o JSON de status geral da UI e do backend.
 */
static void web_build_status_json(const WebUiState *state, char *buffer, size_t buffer_size);
/**
 * @brief Monta o JSON com configuracoes atuais para a aba Settings.
 */
static void web_build_settings_json(const WebUiState *state, char *buffer, size_t buffer_size);
/**
 * @brief Monta o JSON do dashboard dos inversores.
 */
static void web_build_inverters_json(const WebUiState *state, char *buffer, size_t buffer_size);
/**
 * @brief Monta o JSON com portas COM detectadas para a UI.
 */
static void web_build_com_ports_json(WebUiState *state, char *buffer, size_t buffer_size);
/**
 * @brief Monta o JSON de status do monitor serial.
 */
static void web_build_serial_status_json(WebUiState *state, char *buffer, size_t buffer_size);
/**
 * @brief Monta o JSON incremental das linhas do monitor serial.
 */
static void web_build_serial_logs_json(WebUiState *state, unsigned long after_id, char *buffer, size_t buffer_size);
/**
 * @brief Serializa o conteudo textual completo de um monitor para exportacao.
 */
static char *web_build_monitor_log_text(SerialMonitorState *monitor, const char *prefix, size_t *out_length);
/**
 * @brief Monta o JSON de status do monitor CAN bruto.
 */
static void web_build_can_monitor_status_json(WebUiState *state, char *buffer, size_t buffer_size);
/**
 * @brief Monta o JSON incremental do monitor CAN bruto.
 */
static void web_build_can_monitor_logs_json(WebUiState *state, unsigned long after_id, char *buffer, size_t buffer_size);
/**
 * @brief Escapa uma string para uso seguro em JSON.
 */
static void web_json_escape_string(const char *source, char *destination, size_t destination_size);
/**
 * @brief Extrai uma string de um corpo JSON simples.
 */
static int web_json_get_string(const char *body, const char *key, char *destination, size_t destination_size);
/**
 * @brief Extrai um inteiro sem sinal de um corpo JSON simples.
 */
static int web_json_get_uint(const char *body, const char *key, unsigned int *value);
/**
 * @brief Retorna o ponteiro para o corpo de uma requisicao HTTP.
 */
static const char *web_get_request_body(const char *request);
/**
 * @brief Extrai o valor do parametro after de uma URL de monitor.
 */
static unsigned long web_parse_after_id(const char *request);
/**
 * @brief Processa uma conexao HTTP individual da UI web.
 */
static void web_handle_client(WebUiState *state, SOCKET client);
/**
 * @brief Thread principal do servidor HTTP local da UI.
 */
static DWORD WINAPI web_server_thread(LPVOID parameter);

/**
 * @brief Envia uma resposta textual HTTP completa ao cliente.
 *
 * @param client Socket conectado ao cliente HTTP.
 * @param content_type Tipo MIME da resposta.
 * @param body Corpo textual da resposta.
 */
static void web_send_response(SOCKET client, const char *content_type, const char *body)
{
    web_send_response_bytes(client, content_type, body, strlen(body));
}

/**
 * @brief Envia uma resposta HTTP usando um buffer de bytes preexistente.
 *
 * @param client Socket conectado ao cliente HTTP.
 * @param content_type Tipo MIME da resposta.
 * @param body Ponteiro para o corpo da resposta.
 * @param body_length Tamanho do corpo em bytes.
 */
static void web_send_response_bytes(SOCKET client, const char *content_type, const char *body, size_t body_length)
{
    char header[256];
    snprintf(header,
             sizeof(header),
             "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
             content_type,
             (unsigned int)body_length);
    send(client, header, (int)strlen(header), 0);
    if (body != NULL && body_length > 0U) {
        send(client, body, (int)body_length, 0);
    }
}

/**
 * @brief Envia uma resposta HTTP para download de um arquivo de texto.
 *
 * @param client Socket conectado ao cliente HTTP.
 * @param filename Nome sugerido para o arquivo baixado.
 * @param body Conteudo textual a ser baixado.
 * @param body_length Tamanho do conteudo em bytes.
 */
static void web_send_download_response(SOCKET client, const char *filename, const char *body, size_t body_length)
{
    char header[384];
    snprintf(header,
             sizeof(header),
             "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Disposition: attachment; filename=\"%s\"\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
             filename != NULL ? filename : "download.txt",
             (unsigned int)body_length);
    send(client, header, (int)strlen(header), 0);
    if (body != NULL && body_length > 0U) {
        send(client, body, (int)body_length, 0);
    }
}

/**
 * @brief Envia uma resposta HTTP 404 padrao em JSON.
 *
 * @param client Socket conectado ao cliente HTTP.
 */
static void web_send_not_found(SOCKET client)
{
    static const char *body = "{\"error\":\"not_found\"}";
    char header[256];
    snprintf(header, sizeof(header), "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", (unsigned int)strlen(body));
    send(client, header, (int)strlen(header), 0);
    send(client, body, (int)strlen(body), 0);
}

/**
 * @brief Escapa texto para uso seguro em respostas JSON.
 *
 * @param source Texto de origem.
 * @param destination Buffer de destino.
 * @param destination_size Tamanho do buffer de destino.
 */
static void web_json_escape_string(const char *source, char *destination, size_t destination_size)
{
    size_t source_index = 0;
    size_t destination_index = 0;

    if (destination_size == 0) {
        return;
    }

    while (source != NULL && source[source_index] != '\0' && destination_index + 1 < destination_size) {
        char current = source[source_index++];
        if ((current == '\\' || current == '"') && destination_index + 2 < destination_size) {
            destination[destination_index++] = '\\';
            destination[destination_index++] = current;
            continue;
        }
        if ((unsigned char)current < 32U) {
            continue;
        }
        destination[destination_index++] = current;
    }

    destination[destination_index] = '\0';
}

/**
 * @brief Envia um resultado padrao de sucesso ou falha em formato JSON.
 *
 * @param client Socket conectado ao cliente HTTP.
 * @param ok Indica se a operacao foi bem-sucedida.
 * @param message Mensagem humana de retorno.
 */
static void web_send_json_result(SOCKET client, int ok, const char *message)
{
    char body[512];
    char escaped_message[384];
    web_json_escape_string((message != NULL) ? message : "", escaped_message, sizeof(escaped_message));
    snprintf(body, sizeof(body), "{\"ok\":%s,\"message\":\"%s\"}", ok ? "true" : "false", escaped_message);
    web_send_response(client, "application/json; charset=utf-8", body);
}

/**
 * @brief Retorna o ponteiro para o corpo de uma requisicao HTTP.
 *
 * @param request Requisicao HTTP completa.
 * @return const char* Ponteiro para o inicio do corpo ou string vazia.
 */
static const char *web_get_request_body(const char *request)
{
    const char *separator = strstr(request, "\r\n\r\n");
    return (separator != NULL) ? separator + 4 : "";
}

/**
 * @brief Extrai um valor string simples de um corpo JSON reduzido.
 *
 * @param body Corpo JSON recebido.
 * @param key Chave desejada.
 * @param destination Buffer de saida.
 * @param destination_size Tamanho do buffer de saida.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int web_json_get_string(const char *body, const char *key, char *destination, size_t destination_size)
{
    char pattern[64];
    const char *start;
    const char *value_start;
    const char *value_end;
    size_t length;

    if (destination_size == 0) {
        return 0;
    }

    destination[0] = '\0';
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    start = strstr(body, pattern);
    if (start == NULL) {
        return 0;
    }

    value_start = strchr(start + strlen(pattern), ':');
    if (value_start == NULL) {
        return 0;
    }

    value_start = strchr(value_start, '"');
    if (value_start == NULL) {
        return 0;
    }
    value_start++;
    value_end = strchr(value_start, '"');
    if (value_end == NULL) {
        return 0;
    }

    length = (size_t)(value_end - value_start);
    if (length >= destination_size) {
        length = destination_size - 1;
    }
    memcpy(destination, value_start, length);
    destination[length] = '\0';
    return 1;
}

/**
 * @brief Extrai um valor inteiro sem sinal de um corpo JSON reduzido.
 *
 * @param body Corpo JSON recebido.
 * @param key Chave desejada.
 * @param value Ponteiro que recebera o valor extraido.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int web_json_get_uint(const char *body, const char *key, unsigned int *value)
{
    char pattern[64];
    const char *start;
    const char *value_start;
    char *end = NULL;
    unsigned long parsed;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    start = strstr(body, pattern);
    if (start == NULL) {
        return 0;
    }

    value_start = strchr(start + strlen(pattern), ':');
    if (value_start == NULL) {
        return 0;
    }

    value_start++;
    while (*value_start == ' ' || *value_start == '\t') {
        value_start++;
    }

    parsed = strtoul(value_start, &end, 10);
    if (end == value_start) {
        return 0;
    }

    *value = (unsigned int)parsed;
    return 1;
}

/**
 * @brief Monta o JSON de status geral da UI web e do runtime associado.
 *
 * @param state Estado interno da UI web.
 * @param buffer Buffer de saida.
 * @param buffer_size Tamanho do buffer de saida.
 */
static void web_build_status_json(const WebUiState *state, char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size,
             "{\"ui_running\":%s,\"ui_enabled\":%s,\"port\":%u,\"can_opened\":%s,\"can_running\":%s,\"rx_count\":%lu,\"dbc_loaded\":%s,\"can_monitor_enabled\":%s}",
             InterlockedCompareExchange((LONG *)&state->running, 0, 0) ? "true" : "false",
             state->config.enabled ? "true" : "false",
             (unsigned int)state->config.port,
             InterlockedCompareExchange((LONG *)&state->can_opened, 0, 0) ? "true" : "false",
             InterlockedCompareExchange((LONG *)&state->can_running, 0, 0) ? "true" : "false",
             state->rx_count,
             InterlockedCompareExchange((LONG *)&state->dbc_loaded, 0, 0) ? "true" : "false",
             (state->can_config != NULL && state->can_config->ui_can_monitor_enabled) ? "true" : "false");
}

/**
 * @brief Monta o JSON usado para popular a aba Settings.
 *
 * @param state Estado interno da UI web.
 * @param buffer Buffer de saida.
 * @param buffer_size Tamanho do buffer de saida.
 */
static void web_build_settings_json(const WebUiState *state, char *buffer, size_t buffer_size)
{
    const CanConfig *can_config = state->can_config;
    const DbcDatabase *database = state->dbc_database;
    char escaped_com_port[64];
    char escaped_dbc_path[CAN_LOG_PATH_SIZE * 2];
    char escaped_version[128];
    char escaped_serial_com[64];
    char escaped_serial_parity[32];
    char escaped_serial_stop_bits[32];
    char escaped_serial_flow[32];
    char escaped_serial_log[520];

    web_json_escape_string((can_config != NULL) ? can_config->com_port : "", escaped_com_port, sizeof(escaped_com_port));
    web_json_escape_string((can_config != NULL) ? can_config->dbc_vcu_path : "", escaped_dbc_path, sizeof(escaped_dbc_path));
    web_json_escape_string((database != NULL) ? database->version : "", escaped_version, sizeof(escaped_version));
    web_json_escape_string(state->serial_snapshot.com_port, escaped_serial_com, sizeof(escaped_serial_com));
    web_json_escape_string(state->serial_snapshot.parity, escaped_serial_parity, sizeof(escaped_serial_parity));
    web_json_escape_string(state->serial_snapshot.stop_bits, escaped_serial_stop_bits, sizeof(escaped_serial_stop_bits));
    web_json_escape_string(state->serial_snapshot.flow_control, escaped_serial_flow, sizeof(escaped_serial_flow));
    web_json_escape_string(state->serial_snapshot.logfile_path, escaped_serial_log, sizeof(escaped_serial_log));

    snprintf(buffer, buffer_size,
             "{\"com_port\":\"%s\",\"bitrate\":%u,\"dbc_path\":\"%s\",\"dbc_loaded\":%s,\"dbc_version\":\"%s\",\"dbc_messages\":%d,\"dbc_signals\":%d,\"ui_port\":%u,\"ui_enabled\":%s,\"autosave\":%s,"
             "\"serial_com_port\":\"%s\",\"serial_baud_rate\":%u,\"serial_data_bits\":%u,\"serial_parity\":\"%s\",\"serial_stop_bits\":\"%s\",\"serial_flow_control\":\"%s\","
             "\"serial_timestamp_enabled\":%s,\"serial_logfile_enabled\":%s,\"serial_logfile_path\":\"%s\",\"serial_reconnect_enabled\":%s,\"serial_running\":%s,\"can_monitor_enabled\":%s,\"inverter_dbc_loaded\":%s,\"inverter_left_base_set\":%s,\"inverter_right_base_set\":%s,\"inverter_left_base\":%u,\"inverter_right_base\":%u}",
             escaped_com_port,
             (can_config != NULL) ? can_config->bitrate : 0U,
             escaped_dbc_path,
             (database != NULL && database->loaded) ? "true" : "false",
             escaped_version,
             (database != NULL && database->loaded) ? database->message_count : 0,
             (database != NULL && database->loaded) ? database->total_signal_count : 0,
             (unsigned int)state->config.port,
             state->config.enabled ? "true" : "false",
             InterlockedCompareExchange((LONG *)&state->autosave_enabled, 0, 0) ? "true" : "false",
             escaped_serial_com,
             state->serial_snapshot.baud_rate,
             state->serial_snapshot.data_bits,
             escaped_serial_parity,
             escaped_serial_stop_bits,
             escaped_serial_flow,
             state->serial_snapshot.timestamp_enabled ? "true" : "false",
             state->serial_snapshot.logfile_enabled ? "true" : "false",
             escaped_serial_log,
             state->serial_snapshot.reconnect_enabled ? "true" : "false",
             state->serial_snapshot.serial_running ? "true" : "false",
             (can_config != NULL && can_config->ui_can_monitor_enabled) ? "true" : "false",
             (state->inverter_dbc_database != NULL && state->inverter_dbc_database->loaded) ? "true" : "false",
             (can_config != NULL && can_config->inverter_left_base_set) ? "true" : "false",
             (can_config != NULL && can_config->inverter_right_base_set) ? "true" : "false",
             (can_config != NULL) ? can_config->inverter_left_base : 0U,
             (can_config != NULL) ? can_config->inverter_right_base : 0U);
}

/**
 * @brief Monta o JSON com as portas COM detectadas no sistema.
 *
 * @param state Estado interno da UI web.
 * @param buffer Buffer de saida.
 * @param buffer_size Tamanho do buffer de saida.
 */
static void web_build_com_ports_json(WebUiState *state, char *buffer, size_t buffer_size)
{
    char ports[64][32];
    int count = 0;
    int index;
    size_t offset = 0U;

    if (state->callbacks.list_ports != NULL) {
        count = state->callbacks.list_ports(state->callbacks.context, ports, 64);
    }

    offset += (size_t)snprintf(buffer + offset, buffer_size - offset, "{\"ports\":[");
    for (index = 0; index < count && offset + 8U < buffer_size; index++) {
        offset += (size_t)snprintf(buffer + offset,
                                   buffer_size - offset,
                                   "%s\"%s\"",
                                   (index == 0) ? "" : ",",
                                   ports[index]);
    }
    if (offset < buffer_size) {
        snprintf(buffer + offset, buffer_size - offset, "]}");
    }
}

/**
 * @brief Monta o JSON do dashboard de inversores para consumo pela UI.
 *
 * @param state Estado interno da UI web.
 * @param buffer Buffer de saida.
 * @param buffer_size Tamanho do buffer de saida.
 */
static void web_build_inverters_json(const WebUiState *state, char *buffer, size_t buffer_size)
{
    WebUiState *mutable_state = (WebUiState *)state;

    EnterCriticalSection(&mutable_state->lock);
    if (mutable_state->can_config != NULL) {
        mutable_state->inverter_config.left_base_id = mutable_state->can_config->inverter_left_base;
        mutable_state->inverter_config.right_base_id = mutable_state->can_config->inverter_right_base;
        mutable_state->inverter_config.left_base_set = mutable_state->can_config->inverter_left_base_set;
        mutable_state->inverter_config.right_base_set = mutable_state->can_config->inverter_right_base_set;
        mutable_state->inverter_dashboard.left.configured = mutable_state->inverter_config.left_base_set;
        mutable_state->inverter_dashboard.left.command_id = mutable_state->inverter_config.left_base_id;
        mutable_state->inverter_dashboard.left.measures_id = mutable_state->inverter_config.left_base_id + 0x1000U;
        mutable_state->inverter_dashboard.left.states_id = mutable_state->inverter_config.left_base_id + 0x1100U;
        mutable_state->inverter_dashboard.left.gd_id = mutable_state->inverter_config.left_base_id + 0x1200U;
        mutable_state->inverter_dashboard.right.configured = mutable_state->inverter_config.right_base_set;
        mutable_state->inverter_dashboard.right.command_id = mutable_state->inverter_config.right_base_id;
        mutable_state->inverter_dashboard.right.measures_id = mutable_state->inverter_config.right_base_id + 0x1000U;
        mutable_state->inverter_dashboard.right.states_id = mutable_state->inverter_config.right_base_id + 0x1100U;
        mutable_state->inverter_dashboard.right.gd_id = mutable_state->inverter_config.right_base_id + 0x1200U;
    }
    inverter_dashboard_build_json(&mutable_state->inverter_dashboard, &mutable_state->inverter_config, buffer, buffer_size);
    LeaveCriticalSection(&mutable_state->lock);
}

/**
 * @brief Monta o JSON de status resumido do monitor serial web.
 *
 * @param state Estado interno da UI web.
 * @param buffer Buffer de saida.
 * @param buffer_size Tamanho do buffer de saida.
 */
static void web_build_serial_status_json(WebUiState *state, char *buffer, size_t buffer_size)
{
    SerialMonitorSnapshot snapshot;
    char escaped_port[64];

    if (state->serial_monitor != NULL) {
        serial_monitor_get_snapshot(state->serial_monitor, &snapshot);
    } else {
        ZeroMemory(&snapshot, sizeof(snapshot));
    }

    web_json_escape_string(state->serial_snapshot.com_port, escaped_port, sizeof(escaped_port));
    snprintf(buffer,
             buffer_size,
             "{\"running\":%s,\"com_port\":\"%s\",\"baud_rate\":%u,\"buffer_count\":%u,\"earliest_id\":%lu,\"latest_id\":%lu}",
             state->serial_snapshot.serial_running ? "true" : "false",
             escaped_port,
             state->serial_snapshot.baud_rate,
             (unsigned int)snapshot.count,
             snapshot.earliest_id,
             snapshot.latest_id);
}

/**
 * @brief Monta o JSON incremental de linhas do monitor serial web.
 *
 * @param state Estado interno da UI web.
 * @param after_id Ultimo identificador ja consumido pelo frontend.
 * @param buffer Buffer de saida.
 * @param buffer_size Tamanho do buffer de saida.
 */
static void web_build_serial_logs_json(WebUiState *state, unsigned long after_id, char *buffer, size_t buffer_size)
{
    SerialMonitorEntry entries[64];
    char escaped_text[SERIAL_MONITOR_MAX_TEXT_LENGTH * 2];
    char escaped_timestamp[32];
    unsigned long next_after = after_id;
    unsigned long latest_id = 0UL;
    size_t count;
    size_t index;
    size_t used = 0U;

    if (state->serial_monitor == NULL) {
        snprintf(buffer, buffer_size, "{\"latest_id\":0,\"next_after\":0,\"entries\":[]}");
        return;
    }

    count = serial_monitor_copy_since(state->serial_monitor,
                                      after_id,
                                      entries,
                                      sizeof(entries) / sizeof(entries[0]),
                                      &next_after,
                                      &latest_id);

    used += (size_t)snprintf(buffer + used,
                             buffer_size - used,
                             "{\"latest_id\":%lu,\"next_after\":%lu,\"entries\":[",
                             latest_id,
                             next_after);

    for (index = 0U; index < count && used + 64U < buffer_size; index++) {
        const char *highlight = "none";

        if (entries[index].highlight == SERIAL_MONITOR_HIGHLIGHT_INFO) {
            highlight = "info";
        } else if (entries[index].highlight == SERIAL_MONITOR_HIGHLIGHT_WARNING) {
            highlight = "warning";
        } else if (entries[index].highlight == SERIAL_MONITOR_HIGHLIGHT_ERROR) {
            highlight = "error";
        }

        web_json_escape_string(entries[index].text, escaped_text, sizeof(escaped_text));
        web_json_escape_string(entries[index].timestamp, escaped_timestamp, sizeof(escaped_timestamp));
        used += (size_t)snprintf(buffer + used,
                                 buffer_size - used,
                                 "%s{\"id\":%lu,\"timestamp\":\"%s\",\"highlight\":\"%s\",\"text\":\"%s\"}",
                                 (index == 0U) ? "" : ",",
                                 entries[index].id,
                                 escaped_timestamp,
                                 highlight,
                                 escaped_text);
    }

    snprintf(buffer + used, buffer_size - used, "]}");
}

/**
 * @brief Serializa o conteudo textual completo de um monitor para exportacao.
 *
 * @param monitor Monitor a ser serializado.
 * @param prefix Prefixo textual opcional por linha.
 * @param out_length Ponteiro opcional para retornar o tamanho final gerado.
 * @return char* Buffer alocado dinamicamente com o texto exportado.
 */
static char *web_build_monitor_log_text(SerialMonitorState *monitor, const char *prefix, size_t *out_length)
{
    SerialMonitorSnapshot snapshot;
    SerialMonitorEntry *entries;
    unsigned long next_after = 0UL;
    unsigned long latest_id = 0UL;
    size_t count;
    size_t capacity;
    size_t offset = 0U;
    size_t index;
    char *body;

    if (out_length != NULL) {
        *out_length = 0U;
    }
    if (monitor == NULL) {
        return NULL;
    }

    serial_monitor_get_snapshot(monitor, &snapshot);
    if (snapshot.count == 0U) {
        body = (char *)malloc(1U);
        if (body != NULL) {
            body[0] = '\0';
        }
        return body;
    }

    entries = (SerialMonitorEntry *)malloc(snapshot.count * sizeof(SerialMonitorEntry));
    if (entries == NULL) {
        return NULL;
    }

    count = serial_monitor_copy_since(monitor, 0UL, entries, snapshot.count, &next_after, &latest_id);
    capacity = (count * (SERIAL_MONITOR_MAX_TEXT_LENGTH + 64U)) + 1U;
    body = (char *)malloc(capacity);
    if (body == NULL) {
        free(entries);
        return NULL;
    }

    body[0] = '\0';
    for (index = 0U; index < count && offset + 4U < capacity; index++) {
        offset += (size_t)snprintf(body + offset,
                                   capacity - offset,
                                   "[%s] %s%s%s\r\n",
                                   entries[index].timestamp,
                                   (prefix != NULL && prefix[0] != '\0') ? prefix : "",
                                   (prefix != NULL && prefix[0] != '\0') ? " " : "",
                                   entries[index].text);
    }

    free(entries);
    if (out_length != NULL) {
        *out_length = offset;
    }

    return body;
}

/**
 * @brief Monta o JSON de status resumido do monitor CAN bruto.
 *
 * @param state Estado interno da UI web.
 * @param buffer Buffer de saida.
 * @param buffer_size Tamanho do buffer de saida.
 */
static void web_build_can_monitor_status_json(WebUiState *state, char *buffer, size_t buffer_size)
{
    SerialMonitorSnapshot snapshot;

    if (state->can_monitor != NULL) {
        serial_monitor_get_snapshot(state->can_monitor, &snapshot);
    } else {
        ZeroMemory(&snapshot, sizeof(snapshot));
    }

    snprintf(buffer,
             buffer_size,
             "{\"enabled\":%s,\"running\":%s,\"buffer_count\":%u,\"earliest_id\":%lu,\"latest_id\":%lu}",
             (state->can_config != NULL && state->can_config->ui_can_monitor_enabled) ? "true" : "false",
             InterlockedCompareExchange((LONG *)&state->can_running, 0, 0) ? "true" : "false",
             (unsigned int)snapshot.count,
             snapshot.earliest_id,
             snapshot.latest_id);
}

/**
 * @brief Monta o JSON incremental de frames do monitor CAN bruto.
 *
 * @param state Estado interno da UI web.
 * @param after_id Ultimo identificador ja consumido pelo frontend.
 * @param buffer Buffer de saida.
 * @param buffer_size Tamanho do buffer de saida.
 */
static void web_build_can_monitor_logs_json(WebUiState *state, unsigned long after_id, char *buffer, size_t buffer_size)
{
    SerialMonitorEntry entries[64];
    char escaped_text[SERIAL_MONITOR_MAX_TEXT_LENGTH * 2];
    char escaped_timestamp[32];
    unsigned long next_after = after_id;
    unsigned long latest_id = 0UL;
    size_t count;
    size_t index;
    size_t used = 0U;

    if (state->can_monitor == NULL) {
        snprintf(buffer, buffer_size, "{\"latest_id\":0,\"next_after\":0,\"entries\":[]}");
        return;
    }

    count = serial_monitor_copy_since(state->can_monitor,
                                      after_id,
                                      entries,
                                      sizeof(entries) / sizeof(entries[0]),
                                      &next_after,
                                      &latest_id);

    used += (size_t)snprintf(buffer + used,
                             buffer_size - used,
                             "{\"latest_id\":%lu,\"next_after\":%lu,\"entries\":[",
                             latest_id,
                             next_after);

    for (index = 0U; index < count && used + 64U < buffer_size; index++) {
        const char *highlight = "none";

        if (entries[index].highlight == SERIAL_MONITOR_HIGHLIGHT_INFO) {
            highlight = "info";
        } else if (entries[index].highlight == SERIAL_MONITOR_HIGHLIGHT_WARNING) {
            highlight = "warning";
        } else if (entries[index].highlight == SERIAL_MONITOR_HIGHLIGHT_ERROR) {
            highlight = "error";
        }

        web_json_escape_string(entries[index].text, escaped_text, sizeof(escaped_text));
        web_json_escape_string(entries[index].timestamp, escaped_timestamp, sizeof(escaped_timestamp));
        used += (size_t)snprintf(buffer + used,
                                 buffer_size - used,
                                 "%s{\"id\":%lu,\"timestamp\":\"%s\",\"highlight\":\"%s\",\"text\":\"%s\"}",
                                 (index == 0U) ? "" : ",",
                                 entries[index].id,
                                 escaped_timestamp,
                                 highlight,
                                 escaped_text);
    }

    snprintf(buffer + used, buffer_size - used, "]}");
}

/**
 * @brief Extrai o parametro after de uma requisicao GET incremental.
 *
 * @param request Linha inicial da requisicao HTTP.
 * @return unsigned long Valor do parametro after ou 0 quando ausente.
 */
static unsigned long web_parse_after_id(const char *request)
{
    const char *start = strstr(request, "?after=");
    char *end = NULL;

    if (start == NULL) {
        return 0UL;
    }

    start += strlen("?after=");
    return strtoul(start, &end, 10);
}

/**
 * @brief Processa uma conexao HTTP individual recebida pelo servidor local.
 *
 * @param state Estado interno da UI web.
 * @param client Socket conectado ao cliente.
 */
static void web_handle_client(WebUiState *state, SOCKET client)
{
    char request[4096];
    int received;
    char status_json[256];
    char dashboard_json[4096];
    char inverters_json[8192];
    char settings_json[1024];
    char serial_status_json[256];
    char serial_logs_json[49152];
    char can_monitor_status_json[256];
    char can_monitor_logs_json[49152];
    char message[256];
    const char *body;
    char com_port[32];
    char dbc_path[CAN_LOG_PATH_SIZE];
    char action[64];
    unsigned int bitrate;

    received = recv(client, request, sizeof(request) - 1, 0);
    if (received <= 0) {
        return;
    }

    request[received] = '\0';

    if (strncmp(request, "GET / ", 6) == 0 || strncmp(request, "GET /HTTP", 9) == 0) {
        web_send_response(client, "text/html; charset=utf-8", WEB_INDEX_HTML);
        return;
    }

    if (strncmp(request, "GET /styles.css ", 16) == 0) {
        web_send_response(client, "text/css; charset=utf-8", WEB_STYLES_CSS);
        return;
    }

    if (strncmp(request, "GET /app.js ", 12) == 0) {
        web_send_response(client, "application/javascript; charset=utf-8", WEB_APP_JS);
        return;
    }

    if (strncmp(request, "GET /api/status ", 16) == 0) {
        web_build_status_json(state, status_json, sizeof(status_json));
        web_send_response(client, "application/json; charset=utf-8", status_json);
        return;
    }

    if (strncmp(request, "GET /api/vcu ", 13) == 0) {
        EnterCriticalSection(&state->lock);
        vcu_dashboard_build_json(&state->dashboard, dashboard_json, sizeof(dashboard_json));
        LeaveCriticalSection(&state->lock);
        web_send_response(client, "application/json; charset=utf-8", dashboard_json);
        return;
    }

    if (strncmp(request, "GET /api/inverters ", 19) == 0) {
        web_build_inverters_json(state, inverters_json, sizeof(inverters_json));
        web_send_response(client, "application/json; charset=utf-8", inverters_json);
        return;
    }

    if (strncmp(request, "GET /api/settings ", 18) == 0) {
        web_build_settings_json(state, settings_json, sizeof(settings_json));
        web_send_response(client, "application/json; charset=utf-8", settings_json);
        return;
    }

    if (strncmp(request, "GET /api/serial/status ", 23) == 0) {
        web_build_serial_status_json(state, serial_status_json, sizeof(serial_status_json));
        web_send_response(client, "application/json; charset=utf-8", serial_status_json);
        return;
    }

    if (strncmp(request, "GET /api/serial/logs", 20) == 0) {
        web_build_serial_logs_json(state,
                                   web_parse_after_id(request),
                                   serial_logs_json,
                                   sizeof(serial_logs_json));
        web_send_response(client, "application/json; charset=utf-8", serial_logs_json);
        return;
    }

    if (strncmp(request, "GET /api/can-monitor/status ", 28) == 0) {
        web_build_can_monitor_status_json(state, can_monitor_status_json, sizeof(can_monitor_status_json));
        web_send_response(client, "application/json; charset=utf-8", can_monitor_status_json);
        return;
    }

    if (strncmp(request, "GET /api/can-monitor/logs", 25) == 0) {
        web_build_can_monitor_logs_json(state,
                                        web_parse_after_id(request),
                                        can_monitor_logs_json,
                                        sizeof(can_monitor_logs_json));
        web_send_response(client, "application/json; charset=utf-8", can_monitor_logs_json);
        return;
    }

    if (strncmp(request, "GET /api/com-ports ", 19) == 0) {
        char com_ports_json[2048];
        web_build_com_ports_json(state, com_ports_json, sizeof(com_ports_json));
        web_send_response(client, "application/json; charset=utf-8", com_ports_json);
        return;
    }

    if (strncmp(request, "GET /api/serial/export ", 24) == 0) {
        size_t export_length = 0U;
        char *export_body = web_build_monitor_log_text(state->serial_monitor, "[serial]", &export_length);
        if (export_body == NULL) {
            web_send_json_result(client, 0, "Falha ao gerar o log serial.");
            return;
        }
        web_send_download_response(client, "serial_monitor.txt", export_body, export_length);
        free(export_body);
        return;
    }

    if (strncmp(request, "GET /api/can-monitor/export ", 28) == 0) {
        size_t export_length = 0U;
        char *export_body = web_build_monitor_log_text(state->can_monitor, "[can]", &export_length);
        if (export_body == NULL) {
            web_send_json_result(client, 0, "Falha ao gerar o log CAN.");
            return;
        }
        web_send_download_response(client, "can_monitor.txt", export_body, export_length);
        free(export_body);
        return;
    }

    body = web_get_request_body(request);

    if (strncmp(request, "POST /api/settings/serial ", 26) == 0) {
        char serial_parity[16];
        char serial_stop_bits[16];
        char serial_flow[16];
        char serial_log_path[260];
        unsigned int serial_baud_rate = 0U;
        unsigned int serial_data_bits = 0U;

        com_port[0] = '\0';
        serial_parity[0] = '\0';
        serial_stop_bits[0] = '\0';
        serial_flow[0] = '\0';
        serial_log_path[0] = '\0';
        web_json_get_string(body, "com_port", com_port, sizeof(com_port));
        web_json_get_uint(body, "baud_rate", &serial_baud_rate);
        web_json_get_uint(body, "data_bits", &serial_data_bits);
        web_json_get_string(body, "parity", serial_parity, sizeof(serial_parity));
        web_json_get_string(body, "stop_bits", serial_stop_bits, sizeof(serial_stop_bits));
        web_json_get_string(body, "flow_control", serial_flow, sizeof(serial_flow));
        web_json_get_string(body, "logfile_path", serial_log_path, sizeof(serial_log_path));

        if (state->callbacks.apply_serial_settings == NULL) {
            web_send_json_result(client, 0, "Backend de configuracao serial indisponivel.");
            return;
        }

        if (!state->callbacks.apply_serial_settings(state->callbacks.context,
                                                    com_port,
                                                    serial_baud_rate,
                                                    serial_data_bits,
                                                    serial_parity,
                                                    serial_stop_bits,
                                                    serial_flow,
                                                    strstr(body, "\"timestamp_enabled\":true") != NULL,
                                                    strstr(body, "\"logfile_enabled\":true") != NULL,
                                                    serial_log_path,
                                                    strstr(body, "\"reconnect_enabled\":true") != NULL,
                                                    message,
                                                    sizeof(message))) {
            web_send_json_result(client, 0, message);
            return;
        }

        web_send_json_result(client, 1, message);
        return;
    }

    if (strncmp(request, "POST /api/settings/can ", 23) == 0) {
        com_port[0] = '\0';
        bitrate = 0U;
        web_json_get_string(body, "com_port", com_port, sizeof(com_port));
        web_json_get_uint(body, "bitrate", &bitrate);
        if (state->callbacks.apply_can_settings == NULL) {
            web_send_json_result(client, 0, "Backend de configuracao CAN indisponivel.");
            return;
        }
        if (!state->callbacks.apply_can_settings(state->callbacks.context, com_port, bitrate, message, sizeof(message))) {
            web_send_json_result(client, 0, message);
            return;
        }
        web_send_json_result(client, 1, message);
        return;
    }

    if (strncmp(request, "POST /api/settings/dbc/load ", 28) == 0) {
        if (!web_json_get_string(body, "path", dbc_path, sizeof(dbc_path))) {
            web_send_json_result(client, 0, "Informe um caminho DBC valido.");
            return;
        }
        if (state->callbacks.load_dbc == NULL) {
            web_send_json_result(client, 0, "Backend de DBC indisponivel.");
            return;
        }
        if (!state->callbacks.load_dbc(state->callbacks.context, dbc_path, message, sizeof(message))) {
            web_send_json_result(client, 0, message);
            return;
        }
        web_send_json_result(client, 1, message);
        return;
    }

    if (strncmp(request, "POST /api/settings/action ", 26) == 0) {
        if (!web_json_get_string(body, "action", action, sizeof(action))) {
            web_send_json_result(client, 0, "Acao invalida.");
            return;
        }
        if (state->callbacks.execute_action == NULL) {
            web_send_json_result(client, 0, "Backend de acoes indisponivel.");
            return;
        }
        if (!state->callbacks.execute_action(state->callbacks.context, action, message, sizeof(message))) {
            web_send_json_result(client, 0, message);
            return;
        }
        web_send_json_result(client, 1, message);
        return;
    }

    web_send_not_found(client);
}

/**
 * @brief Executa o loop principal do servidor HTTP local da UI web.
 *
 * @param parameter Ponteiro para o estado da UI web.
 * @return DWORD Codigo de saida da thread.
 */
static DWORD WINAPI web_server_thread(LPVOID parameter)
{
    WebUiState *state = (WebUiState *)parameter;
    SOCKET listen_socket = (SOCKET)state->listen_socket;

    while (InterlockedCompareExchange(&state->running, 0, 0)) {
        fd_set read_set;
        struct timeval timeout;
        int ready;

        FD_ZERO(&read_set);
        FD_SET(listen_socket, &read_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;
        ready = select(0, &read_set, NULL, NULL, &timeout);
        if (ready > 0 && FD_ISSET(listen_socket, &read_set)) {
            SOCKET client = accept(listen_socket, NULL, NULL);
            if (client != INVALID_SOCKET) {
                web_handle_client(state, client);
                closesocket(client);
            }
        }
    }

    return 0;
}

/**
 * @brief Define os valores padrao de configuracao da UI web.
 *
 * @param config Estrutura de configuracao a ser inicializada.
 */
void web_ui_set_default_config(WebUiConfig *config)
{
    config->enabled = 0;
    config->port = WEB_UI_DEFAULT_PORT;
}

/**
 * @brief Aplica uma entrada de configuracao da UI web lida do arquivo de config.
 *
 * @param config Estrutura de configuracao a ser atualizada.
 * @param key Chave lida do arquivo.
 * @param value Valor textual associado a chave.
 * @return int Retorna 1 quando a chave foi reconhecida e aplicada.
 */
int web_ui_apply_config_entry(WebUiConfig *config, const char *key, const char *value)
{
    char *end = NULL;
    unsigned long port;

    if (_stricmp(key, "ui_enabled") == 0) {
        config->enabled = (_stricmp(value, "on") == 0 || _stricmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        return 1;
    }

    if (_stricmp(key, "ui_port") == 0) {
        port = strtoul(value, &end, 10);
        if (end == value || *end != '\0' || port == 0UL || port > 65535UL) {
            return 0;
        }
        config->port = (unsigned short)port;
        return 1;
    }

    return 0;
}

/**
 * @brief Persiste a configuracao atual da UI web em arquivo.
 *
 * @param file Arquivo de destino aberto para escrita.
 * @param config Estrutura de configuracao a ser persistida.
 */
void web_ui_save_config(FILE *file, const WebUiConfig *config)
{
    fprintf(file, "ui_enabled=%s\n", config->enabled ? "on" : "off");
    fprintf(file, "ui_port=%u\n", (unsigned int)config->port);
}

/**
 * @brief Inicializa o estado completo da UI web.
 *
 * @param state Estrutura principal da UI web.
 */
void web_ui_initialize(WebUiState *state)
{
    ZeroMemory(state, sizeof(*state));
    web_ui_set_default_config(&state->config);
    vcu_dashboard_reset(&state->dashboard);
    inverter_dashboard_set_default_config(&state->inverter_config);
    inverter_dashboard_reset(&state->inverter_dashboard);
    state->serial_monitor = NULL;
    state->can_monitor = NULL;
    InitializeCriticalSection(&state->lock);
    state->listen_socket = (UINT_PTR)INVALID_SOCKET;
}

/**
 * @brief Conecta a UI web aos runtimes de serial, CAN, DBC e callbacks do terminal.
 *
 * @param state Estrutura principal da UI web.
 * @param can_config Configuracao atual do CAN.
 * @param dbc_database Banco DBC da VCU.
 * @param inverter_dbc_database Banco DBC dos inversores.
 * @param serial_monitor Monitor serial bruto.
 * @param can_monitor Monitor CAN bruto.
 * @param callbacks Conjunto de callbacks usados pela UI para acoes remotas.
 */
void web_ui_bind_runtime(WebUiState *state,
                         const CanConfig *can_config,
                         const DbcDatabase *dbc_database,
                         const DbcDatabase *inverter_dbc_database,
                         SerialMonitorState *serial_monitor,
                         SerialMonitorState *can_monitor,
                         const WebUiCallbacks *callbacks)
{
    state->can_config = can_config;
    state->dbc_database = dbc_database;
    state->inverter_dbc_database = inverter_dbc_database;
    state->serial_monitor = serial_monitor;
    state->can_monitor = can_monitor;
    if (callbacks != NULL) {
        state->callbacks = *callbacks;
    }
}

/**
 * @brief Finaliza a UI web e libera seus recursos internos.
 *
 * @param state Estrutura principal da UI web.
 */
void web_ui_destroy(WebUiState *state)
{
    web_ui_stop(state);
    DeleteCriticalSection(&state->lock);
}

/**
 * @brief Inicia o servidor HTTP local da UI web.
 *
 * @param state Estrutura principal da UI web.
 * @param database Banco DBC da VCU usado como prerequisito quando aplicavel.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int web_ui_start(WebUiState *state, const DbcDatabase *database)
{
    WSADATA wsa_data;
    SOCKET listen_socket;
    struct sockaddr_in address;

    if ((database == NULL || !database->loaded) &&
        !(state->can_config != NULL && state->can_config->ui_can_monitor_enabled)) {
        SetLastError(ERROR_INVALID_STATE);
        return 0;
    }

    if (InterlockedCompareExchange(&state->running, 0, 0)) {
        return 1;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        SetLastError(ERROR_OPEN_FAILED);
        return 0;
    }

    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        WSACleanup();
        SetLastError(ERROR_OPEN_FAILED);
        return 0;
    }

    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(state->config.port);

    if (bind(listen_socket, (const struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR ||
        listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listen_socket);
        WSACleanup();
        SetLastError(ERROR_ADDRESS_ALREADY_ASSOCIATED);
        return 0;
    }

    state->listen_socket = (UINT_PTR)listen_socket;
    InterlockedExchange(&state->running, 1);
    state->thread = CreateThread(NULL, 0, web_server_thread, state, 0, &state->thread_id);
    if (state->thread == NULL) {
        InterlockedExchange(&state->running, 0);
        closesocket(listen_socket);
        state->listen_socket = (UINT_PTR)INVALID_SOCKET;
        WSACleanup();
        return 0;
    }

    return 1;
}

/**
 * @brief Interrompe o servidor HTTP local da UI web.
 *
 * @param state Estrutura principal da UI web.
 */
void web_ui_stop(WebUiState *state)
{
    SOCKET listen_socket;

    if (!InterlockedCompareExchange(&state->running, 0, 0)) {
        return;
    }

    InterlockedExchange(&state->running, 0);
    listen_socket = (SOCKET)state->listen_socket;
    if (listen_socket != INVALID_SOCKET) {
        closesocket(listen_socket);
        state->listen_socket = (UINT_PTR)INVALID_SOCKET;
    }

    if (state->thread != NULL) {
        WaitForSingleObject(state->thread, INFINITE);
        CloseHandle(state->thread);
        state->thread = NULL;
        state->thread_id = 0;
    }

    WSACleanup();
}

/**
 * @brief Abre a UI web no navegador padrao do sistema.
 *
 * @param state Estrutura principal da UI web.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int web_ui_open_browser(const WebUiState *state)
{
    char url[128];
    HINSTANCE result;

    snprintf(url, sizeof(url), "http://127.0.0.1:%u/", (unsigned int)state->config.port);
    result = ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)result > 32) ? 1 : 0;
}

/**
 * @brief Atualiza os dashboards da UI a partir de um frame CAN recebido.
 *
 * @param state Estrutura principal da UI web.
 * @param database Banco DBC da VCU.
 * @param frame Frame CAN recebido.
 */
void web_ui_update_from_frame(WebUiState *state, const DbcDatabase *database, const CanFrame *frame)
{
    EnterCriticalSection(&state->lock);
    state->rx_count++;
    vcu_dashboard_update_from_frame(&state->dashboard, database, frame);
    if (state->can_config != NULL) {
        state->inverter_config.left_base_id = state->can_config->inverter_left_base;
        state->inverter_config.right_base_id = state->can_config->inverter_right_base;
        state->inverter_config.left_base_set = state->can_config->inverter_left_base_set;
        state->inverter_config.right_base_set = state->can_config->inverter_right_base_set;
    }
    inverter_dashboard_update_from_frame(&state->inverter_dashboard, &state->inverter_config, state->inverter_dbc_database, frame);
    LeaveCriticalSection(&state->lock);
}

/**
 * @brief Reinicializa o dashboard VCU e o dashboard dos inversores.
 *
 * @param state Estrutura principal da UI web.
 */
void web_ui_reset_dashboard(WebUiState *state)
{
    EnterCriticalSection(&state->lock);
    vcu_dashboard_reset(&state->dashboard);
    inverter_dashboard_reset(&state->inverter_dashboard);
    LeaveCriticalSection(&state->lock);
}

/**
 * @brief Reinicializa apenas o dashboard dos inversores.
 *
 * @param state Estrutura principal da UI web.
 */
void web_ui_reset_inverters(WebUiState *state)
{
    EnterCriticalSection(&state->lock);
    inverter_dashboard_reset(&state->inverter_dashboard);
    LeaveCriticalSection(&state->lock);
}

/**
 * @brief Imprime no console um resumo do estado atual da UI web.
 *
 * @param state Estrutura principal da UI web.
 * @param database Banco DBC da VCU atualmente associado.
 */
void web_ui_print_status(const WebUiState *state, const DbcDatabase *database)
{
    printf("UI web:\n");
    printf("  Estado: %s\n", InterlockedCompareExchange((LONG *)&state->running, 0, 0) ? "ativa" : "parada");
    printf("  Habilitada: %s\n", state->config.enabled ? "on" : "off");
    printf("  Porta: %u\n", (unsigned int)state->config.port);
    printf("  URL: http://127.0.0.1:%u/\n", (unsigned int)state->config.port);
    printf("  DBC carregado: %s\n", (database != NULL && database->loaded) ? "sim" : "nao");
    printf("  CAN aberto: %s\n", InterlockedCompareExchange((LONG *)&state->can_opened, 0, 0) ? "sim" : "nao");
    printf("  CAN ativo: %s\n", InterlockedCompareExchange((LONG *)&state->can_running, 0, 0) ? "sim" : "nao");
    printf("  RX total: %lu\n", state->rx_count);
}

/**
 * @brief Atualiza o estado de runtime do subsistema CAN visivel para a UI.
 *
 * @param state Estrutura principal da UI web.
 * @param can_opened Indica se a interface CAN esta aberta.
 * @param can_running Indica se a captura CAN esta ativa.
 */
void web_ui_set_can_runtime(WebUiState *state, int can_opened, int can_running)
{
    InterlockedExchange(&state->can_opened, can_opened ? 1 : 0);
    InterlockedExchange(&state->can_running, can_running ? 1 : 0);
}

/**
 * @brief Atualiza o estado de autosave exposto para a UI web.
 *
 * @param state Estrutura principal da UI web.
 * @param enabled Indica se o autosave esta habilitado.
 */
void web_ui_set_autosave_enabled(WebUiState *state, int enabled)
{
    InterlockedExchange(&state->autosave_enabled, enabled ? 1 : 0);
}

/**
 * @brief Atualiza o snapshot serial usado pela aba Settings e pelo monitor web.
 *
 * @param state Estrutura principal da UI web.
 * @param snapshot Snapshot serial mais recente.
 */
void web_ui_set_serial_snapshot(WebUiState *state, const WebUiSerialSnapshot *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    state->serial_snapshot = *snapshot;
}
