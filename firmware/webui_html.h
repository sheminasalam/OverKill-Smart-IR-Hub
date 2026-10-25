#pragma once
#include <Arduino.h>

// v12 WebUI served from PROGMEM as a raw literal. No dynamic RAM.
// The raw-literal delimiter below avoids escaping quotes in the HTML/JS.
static const char WEBUI_HTML[] PROGMEM = R"=====(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Universal IR Hub</title>
<script>
(function(){
  try {
    var n = localStorage.getItem('irHubName');
    if (n) document.title = n;
  } catch(e) {}
})();
</script>
<style>
:root{--bg:#070b0f;--shell:#0b1117;--shell2:#101822;--card1:#18222d;--card2:#131b23;--edge:#4b5b6c;--edge-soft:#34404d;--text:#f4f7fa;--muted:#9aa5b0;--muted2:#707b86;--blue:#315ff2;--blue2:#4c7cff;--green:#20c76a;--green2:#36db7b;--red:#d95858;--red2:#ef6767;--gray:#626c78;--gray2:#75818d;--purple:#9669ff;--shadow:0 10px 30px rgba(0,0,0,.34);--glow-blue:0 0 28px rgba(58,105,255,.16);--glow-green:0 0 28px rgba(40,224,119,.13)}
*{box-sizing:border-box}
html,body{margin:0;padding:0;min-height:100%;background:radial-gradient(circle at 50% 0%,#17202a 0,#0e141a 34%,#070b0f 75%);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
body{display:flex;justify-content:center;overflow-x:hidden}
button,input,select{font:inherit}
button{border:0;cursor:pointer}
button:disabled{cursor:default;opacity:.45}
.app{position:relative;width:100%;max-width:520px;min-height:100vh;padding:max(17px,env(safe-area-inset-top)) 14px max(18px,env(safe-area-inset-bottom));background:linear-gradient(180deg,rgba(16,22,29,.97),rgba(5,8,11,.985) 48%,rgba(5,8,11,.995))}
/* Top-of-viewport page loading bar */
#pageLoader{
  position:fixed;
  top:0;left:0;right:0;
  height:3px;
  z-index:999;
  pointer-events:none;
  opacity:0;
  transition:opacity .18s ease-out;
}
#pageLoader.active{opacity:1}
#pageLoaderBar{
  width:0%;
  height:100%;
  background:linear-gradient(90deg,#426cff,#4c7cff,#54e290);
  box-shadow:0 0 10px rgba(74,124,255,.55);
  transition:width .25s ease-out;
}
.page{display:none;animation:pageIn .12s ease-out}
.page.active{display:block}
@keyframes pageIn{from{opacity:.6;transform:translateY(3px)}to{opacity:1;transform:translateY(0)}}
.topbar{display:flex;align-items:center;gap:9px;min-height:34px}
.eyebrow{color:#7f8994;font-size:15px;letter-spacing:1.2px;text-transform:uppercase;line-height:1.1}
.back{width:46px;height:46px;flex:0 0 46px;display:grid;place-items:center;padding:0;border-radius:50%;border:1px solid #26333f;background:radial-gradient(circle at 45% 35%,#26323d,#0c1319);color:#aeb9c3;line-height:1;box-shadow:0 5px 18px rgba(0,0,0,.22)}
.back:hover{color:#fff;border-color:#536477}
.backIcon{width:31px;height:31px;display:block;fill:none;stroke:currentColor;stroke-width:2.35;stroke-linecap:round;stroke-linejoin:round}
.titleRow{display:flex;align-items:center;justify-content:space-between;gap:10px}
.title{margin:2px 0 16px;font-size:31px;line-height:1.05;letter-spacing:-.7px;font-weight:760}
.titleIcon{flex:0 0 auto;width:27px;height:27px;color:#84909c}
.homeTitle{margin-top:3px}
.homeDeviceRow{display:flex;align-items:center;justify-content:space-between;gap:10px;width:100%;margin:7px 0 22px}
.homeDeviceName{min-width:0;flex:1;text-align:left;color:var(--text);font-size:31px;line-height:1.05;letter-spacing:-.7px;font-weight:760;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.homeStatusPill{flex:0 0 auto;margin:0;min-height:39px;padding:4px 13px 4px 10px;font-size:18px}
.statusPill{display:inline-flex;align-items:center;gap:9px;min-height:42px;padding:5px 17px 5px 12px;border-radius:22px;border:1px solid #526171;background:linear-gradient(180deg,#202d3a,#151d26);box-shadow:var(--glow-blue);color:#d7dfe6;font-size:20px;margin:5px auto 21px}
.dot{width:10px;height:10px;border-radius:50%;background:#bddcff;box-shadow:0 0 9px rgba(146,196,255,.8)}
.dot.green{background:#4fe28c;box-shadow:0 0 11px rgba(48,234,132,.7)}
.card{position:relative;width:100%;margin:0 0 12px;padding:13px 13px;border:1px solid var(--edge);border-radius:14px;background:linear-gradient(135deg,rgba(40,52,64,.8),rgba(17,24,31,.92));box-shadow:var(--shadow),var(--glow-blue)}
.card.clickable{display:flex;align-items:center;justify-content:space-between;min-height:78px;text-align:left;color:var(--text);transition:.14s ease}
.card.clickable:hover{transform:translateY(-1px);border-color:#6485a9}
.cardMain{display:flex;align-items:center;gap:13px;min-width:0}
.iconCircle{width:46px;height:46px;flex:0 0 46px;display:grid;place-items:center;border-radius:50%;color:#abb5be;background:radial-gradient(circle at 45% 35%,#26323d,#0c1319);border:1px solid #26333f}
.iconCircle.red{color:#ff6565}
.iconCircle.green{color:#42dc88}
.iconCircle.blue{color:#78a8ff}
.icon{width:23px;height:23px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}
.cardLabel{font-size:20px;line-height:1.1;font-weight:520;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.chevron{color:#8c98a3;font-size:31px;font-weight:300;padding-left:9px}
.actionCard{display:flex;align-items:center;justify-content:center;min-height:64px;border-style:dashed;color:#79a8f5;background:rgba(35,61,92,.22);box-shadow:var(--glow-blue)}
.actionCard.greenAction{color:#50d881;background:rgba(29,104,66,.12);border-color:#4acb83;box-shadow:var(--glow-green)}
.actionCard span{font-size:19px;font-weight:500}
.plus{font-size:29px;line-height:0;margin-right:7px;font-weight:300}
.secondaryRow{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:4px}
.secondary{min-height:61px;display:flex;align-items:center;justify-content:center;gap:8px;border:1px solid #4c5965;border-radius:11px;background:linear-gradient(180deg,#232e39,#141c23);color:#c7cdd2;box-shadow:var(--shadow);font-size:17px}
.secondary:hover{border-color:#6f7e8d}
.secondaryIcon{width:20px;height:20px;color:#b1bac2}
.deleteFull{min-height:61px;margin-top:12px;width:100%;border:1px solid rgba(229,90,90,.72);border-radius:11px;background:rgba(67,16,18,.1);color:#db6060;font-size:18px}
.deleteFull:hover{border-color:var(--red2);color:#ff7c7c}
.version{margin-top:17px;padding-top:11px;border-top:1px solid #283038;text-align:right;color:#7c858e;font-size:13px}
.pageBack{margin-bottom:4px}
.sectionCard{padding:0;overflow:hidden}
.sectionHead{display:flex;align-items:center;gap:9px;padding:10px 12px 8px;border-bottom:1px solid rgba(113,128,143,.27)}
.sectionHead .icon{width:20px;height:20px;color:#aab3bb}
.sectionHead strong{font-size:18px}
.sectionBody{padding:8px 12px 11px}
.infoRow{display:grid;grid-template-columns:1fr auto;gap:8px;margin:2px 0;color:#aeb7c0;font-size:16px}
.infoRow span:last-child{color:#f4f6f8;text-align:right;font-weight:500}
.infoRow .ok{color:#55df8a!important}
.infoRow .redText{color:#f17070!important}
.commandCard{padding:0;overflow:hidden}
.commandTop{display:flex;align-items:center;justify-content:space-between;gap:9px;padding:11px 12px}
.commandIdentity{display:flex;align-items:center;gap:11px;min-width:0}
.commandIcon{width:43px;height:43px;flex:0 0 43px;border-radius:50%;display:grid;place-items:center;color:#aeb9c3;background:radial-gradient(circle at 45% 35%,#26323d,#0c1319)}
.commandName{font-size:21px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.commandActions{display:flex;gap:8px;flex:0 0 auto}
.pillBtn{min-width:92px;min-height:43px;padding:0 15px;border-radius:23px;color:#fff;font-size:17px;box-shadow:0 5px 18px rgba(0,0,0,.28)}
.pressBtn{background:linear-gradient(180deg,#426cff,#284de0);box-shadow:0 0 22px rgba(60,101,255,.23),0 5px 18px rgba(0,0,0,.28)}
.pressBtn:hover{background:linear-gradient(180deg,#5079ff,#2b50df)}
.editBtn{min-width:43px;width:43px;padding:0;display:grid;place-items:center;background:linear-gradient(180deg,#767f88,#5e6873);color:#edf1f4}
.editBtn:hover{background:linear-gradient(180deg,#87919a,#68727d)}
.editBtn .icon{width:22px;height:22px}
.formCard{padding:16px}
.formLabel{color:#a0aab4;font-size:17px;margin:0 0 7px}
.textInput,.selectInput{width:100%;height:53px;padding:0 14px;border:1px solid #27323d;border-radius:12px;outline:0;color:#f4f7fa;background:#090e12;box-shadow:inset 0 0 0 1px rgba(255,255,255,.02)}
.textInput:focus,.selectInput:focus{border-color:#5f7d9d}
.inputWithIcon{position:relative}
.inputWithIcon .textInput{padding-right:48px}
.inputEditIcon{position:absolute;right:14px;top:50%;transform:translateY(-50%);width:21px;height:21px;color:#8b959f}
.toggleRow{display:flex;align-items:center;gap:12px;padding:14px 0;border-top:1px solid rgba(121,132,143,.2)}
.toggleText{flex:1}
.toggleTitle{font-size:18px;margin-bottom:2px}
.toggleSub{color:#8f99a2;font-size:14px}
.toggle{position:relative;width:65px;height:37px;border-radius:20px;border:1px solid #65707b;background:#5d6874}
.toggle.on{background:#3acb79;border-color:#54e290}
.toggle:after{content:"";position:absolute;width:29px;height:29px;top:3px;left:3px;border-radius:50%;background:#fff;box-shadow:0 2px 5px rgba(0,0,0,.35);transition:.15s}
.toggle.on:after{left:31px}
.relearnBtn{width:100%;min-height:51px;border:1px solid #7aa0c6;border-radius:12px;background:rgba(53,81,108,.16);color:#90b7e4;font-size:18px}
.relearnBtn:hover{background:rgba(58,86,116,.3)}
.saveFull{width:100%;min-height:61px;margin-top:17px;border-radius:12px;background:linear-gradient(180deg,#25cc70,#11ab58);color:#fff;font-size:19px;box-shadow:0 0 24px rgba(24,205,103,.13)}
.saveFull:hover{background:linear-gradient(180deg,#34da7d,#10b15a)}
.fieldGroup{margin-bottom:14px}
.fieldGroup:last-child{margin-bottom:0}
.checkboxRow{display:flex;align-items:center;gap:10px;margin-top:8px;color:#dfe4e8;font-size:16px}
.checkbox{appearance:none;width:22px;height:22px;border-radius:6px;border:1px solid #68737e;background:#0a0f14;position:relative}
.checkbox:checked{background:#2dc870;border-color:#4ce38a}
.checkbox:checked:after{content:"";position:absolute;left:6px;top:2px;width:6px;height:11px;border:solid #fff;border-width:0 2px 2px 0;transform:rotate(45deg)}
.scanBtn{margin-top:8px;width:100%;min-height:47px;border-radius:10px;background:linear-gradient(180deg,#2b3540,#202932);border:1px solid #4e5c69;color:#cfd6dc}
.scanStatus{color:#7f8b96;font-size:13px;margin-top:7px}
.smallNote{color:#848f99;font-size:13px;line-height:1.4;margin-top:8px}
.orderRow{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:11px 12px;border:1px solid #40505e;border-radius:12px;background:linear-gradient(135deg,rgba(37,48,59,.75),rgba(16,23,30,.9));margin-bottom:9px}
.orderBtns{display:flex;gap:4px;margin-left:6px}
.orderBtns button{min-width:32px;min-height:32px;border-radius:8px;background:#26323d;border:1px solid #4e5b68;color:#d3dae0;font-size:16px;line-height:1;padding:0}
.orderBtns button:hover{background:#31404c}
.orderBtns button:disabled{opacity:.35}
.idBadge{
  display:none;
  margin-left:6px;
  padding:2px 7px;
  border-radius:6px;
  font-size:11px;
  font-weight:600;
  letter-spacing:.4px;
  color:#aeb9c3;
  background:#0a0f14;
  border:1px solid #37424d;
  white-space:nowrap;
}
.page.arrange-mode .idBadge{display:inline-block}
/* Arrange mode toggle */
.arrangeToggle{
  margin-left:auto;
  padding:0 14px;
  min-height:34px;
  border-radius:17px;
  border:1px solid #4e5c69;
  background:linear-gradient(180deg,#2b3540,#202932);
  color:#cfd6dc;
  font-size:14px;
  font-weight:500;
  display:inline-flex;
  align-items:center;
}
.arrangeToggle.on{
  border-color:#54e290;
  color:#54e290;
  background:linear-gradient(180deg,#1f3a2a,#152819);
}

/* Normal mode: only Press is visible; arrows and Edit hidden */
.page .orderBtns{display:none}
.page .editBtn{display:none}

/* Edit mode: show arrows and the pencil; hide Press */
.page.arrange-mode .orderBtns{display:flex}
.page.arrange-mode .editBtn{display:grid}
.page.arrange-mode .pressBtn{display:none}

/* Give the identity blocks priority over the action buttons */
.commandTop .commandIdentity{flex:1;min-width:0}
.card.clickable .cardMain{flex:1;min-width:0}
.commandIdentity .scrollWrap{flex:1;min-width:0}
.cardMain .scrollWrap{flex:1;min-width:0}

/* Auto-scroll for names that overflow their container */
.scrollWrap{
  overflow:hidden;
  position:relative;
  display:block;
  min-width:0;
}
.scrollWrap.fade{
  -webkit-mask-image:linear-gradient(90deg,#000 0,#000 calc(100% - 18px),transparent 100%);
          mask-image:linear-gradient(90deg,#000 0,#000 calc(100% - 18px),transparent 100%);
}
.scrollInner{
  display:inline-block;
  white-space:nowrap;
  will-change:transform;
}
.scrollInner.scrolling{
  animation:marqueeX 8s linear infinite;
}
@keyframes marqueeX{
  0%, 10%   { transform:translateX(0); }
  50%, 60%  { transform:translateX(calc(-1 * var(--scroll-max, 0px))); }
  100%      { transform:translateX(0); }
}
.orderName{display:flex;align-items:center;gap:10px;min-width:0;font-size:18px}
.moveBtns{display:flex;gap:6px}
.moveBtns button{min-width:46px;min-height:40px;border-radius:9px;background:#26323d;border:1px solid #4e5b68;color:#d3dae0;font-size:14px}
.moveBtns button:hover{background:#31404c}
.empty{text-align:center;padding:16px 4px;color:#89949e}
.modal{display:none;position:fixed;inset:0;z-index:50;align-items:center;justify-content:center;padding:20px;background:rgba(0,0,0,.67);backdrop-filter:blur(4px)}
.modal.show{display:flex}
.modalBox{width:100%;max-width:470px;padding:18px;border:1px solid #526274;border-radius:18px;background:linear-gradient(145deg,#1d2935,#0e141a);box-shadow:0 24px 60px rgba(0,0,0,.55),0 0 32px rgba(58,99,255,.14)}
.modalTitle{font-size:24px;margin:0 0 12px}
.learnVisual{display:grid;place-items:center;padding:10px 0 8px}
.learnOrb{width:72px;height:72px;border-radius:50%;display:grid;place-items:center;border:1px solid #627487;background:radial-gradient(circle,#27384a,#10171e);box-shadow:0 0 34px rgba(75,129,255,.22)}
.learnOrb .icon{width:34px;height:34px;color:#8eb8f2}
.learnProgressWrap{width:100%;height:6px;background:rgba(255,255,255,.08);border-radius:3px;overflow:hidden;margin:6px 0 16px}
.learnProgressBar{width:0%;height:100%;background:linear-gradient(90deg,#426cff,#4c7cff);border-radius:3px;transition:width 12s linear}
.learnProgressBar.fast{transition:width .2s ease-out}
.learnProgressBar.done{background:linear-gradient(90deg,#25cc70,#11ab58)}
.learnText{text-align:center;color:#cbd3da;font-size:16px;line-height:1.5;padding:0 7px 10px}
.learnSuccess{text-align:center;color:#54df8c;font-size:21px;font-weight:700;padding:9px 0 13px}
.modalActions{display:grid;grid-template-columns:1fr 1fr;gap:9px}
.modalActions button{min-height:48px;border-radius:11px;font-size:16px}
.fullBtn{width:100%;min-height:53px;border-radius:12px;margin-top:7px;font-size:17px}
.redOutline{background:transparent;color:#e26666;border:1px solid rgba(226,102,102,.75)}
.redOutline:hover{background:rgba(115,25,25,.16)}
.mutedBtn{background:linear-gradient(180deg,#252f39,#182027);color:#c2cbd2;border:1px solid #45525e}
.mutedBtn:hover{background:linear-gradient(180deg,#2c3843,#1d2730)}
.deviceHeader{display:flex;align-items:center;gap:11px;margin:2px 0 15px}
.deviceHeader .headerIcon{width:29px;height:29px;color:#929da7}
.deviceConnection{display:flex;align-items:center;gap:7px;margin-left:auto;color:#5bdd8a;font-size:15px}
.pageIcon{color:#87929d;width:28px;height:28px}
@media(max-width:420px){.app{padding-left:12px;padding-right:12px}.title{font-size:29px}.homeDeviceName{font-size:29px}.homeStatusPill{font-size:17px}.cardLabel{font-size:19px}.commandName{font-size:20px}.pillBtn{min-width:88px;padding:0 12px}}
@media(max-width:360px){.app{padding-left:10px;padding-right:10px}.title{font-size:27px}.homeDeviceName{font-size:25px}.homeStatusPill{font-size:16px;padding-left:8px;padding-right:10px}.homeStatusPill .dot{width:8px;height:8px}.secondaryRow{gap:8px}.secondary{font-size:15px}.commandActions{gap:6px}.pillBtn{min-width:78px;font-size:15px;padding:0 9px}}
:root[data-theme="light"]{--bg:#edf1f4;--shell:#f5f7f9;--shell2:#ffffff;--card1:#f8fafc;--card2:#e9eef2;--edge:#aebbc6;--edge-soft:#c6d0d8;--text:#18222b;--muted:#5e6b76;--muted2:#7d8993;--blue:#315fe0;--blue2:#4c7cff;--green:#159c54;--green2:#22b966;--red:#c94949;--red2:#dd5a5a;--gray:#697580;--gray2:#7d8993;--purple:#8055dd;--shadow:0 8px 24px rgba(35,52,68,.13);--glow-blue:0 0 22px rgba(49,95,224,.09);--glow-green:0 0 22px rgba(21,156,84,.08)}
:root[data-theme="light"] body{background:linear-gradient(180deg,#dfe5e9,#eef2f5 40%,#f4f6f8)}
:root[data-theme="light"] .app{background:linear-gradient(180deg,rgba(248,250,252,.98),rgba(239,243,246,.99))}
:root[data-theme="light"] .card{background:linear-gradient(135deg,#ffffff,#eef2f5)}
:root[data-theme="light"] .iconCircle,:root[data-theme="light"] .commandIcon{background:radial-gradient(circle at 45% 35%,#edf2f5,#dce3e9);border-color:#c2ccd5}
:root[data-theme="light"] .secondary{background:linear-gradient(180deg,#f6f8fa,#e6ebef);color:#3d4a55;border-color:#b3bec8}
:root[data-theme="light"] .scanBtn,:root[data-theme="light"] .orderRow{background:linear-gradient(180deg,#f5f7f9,#e7ecef)}
:root[data-theme="light"] .orderRow{border-color:#b6c1ca}
:root[data-theme="light"] .moveBtns button{background:#e2e8ed;border-color:#b3bec8;color:#34434e}
:root[data-theme="light"] .textInput,:root[data-theme="light"] .selectInput{background:#fff;color:#17212a;border-color:#bcc7d0}
:root[data-theme="light"] .modalBox{background:linear-gradient(145deg,#ffffff,#edf1f4);border-color:#adb9c4;box-shadow:0 24px 60px rgba(25,39,52,.22)}
:root[data-theme="light"] .learnOrb{background:radial-gradient(circle,#edf3f7,#dfe7ed);border-color:#aab8c5}
.themeToggle{width:46px;height:46px;flex:0 0 46px;display:grid;place-items:center;padding:0;border-radius:50%;border:1px solid #394754;background:radial-gradient(circle at 45% 35%,#26323d,#0c1319);color:#d9e4ef;box-shadow:0 5px 18px rgba(0,0,0,.22)}
.homeTemp{display:none;align-items:center;gap:5px;margin-right:4px;padding:6px 12px;border-radius:20px;background:linear-gradient(180deg,#202d3a,#151d26);border:1px solid #526171;color:#e6ecf2;font-size:17px;font-weight:600;white-space:nowrap;box-shadow:0 3px 12px rgba(0,0,0,.22)}
.homeTemp.visible{display:inline-flex}
.homeTemp svg{width:16px;height:16px;color:#78a8ff;flex:0 0 16px}
:root[data-theme="light"] .homeTemp{background:linear-gradient(180deg,#f6f8fa,#e6ebef);border-color:#b3bec8;color:#3d4a55}
:root[data-theme="light"] .homeTemp svg{color:#315fe0}
@media(max-width:360px){.homeTemp{font-size:15px;padding:5px 9px}.homeTemp svg{width:14px;height:14px;flex-basis:14px}}
.themeToggle:hover{border-color:#65788c;color:#fff}
.themeIcon{width:26px;height:26px;display:block;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}
.automationCommandRow{display:grid;grid-template-columns:minmax(0,1fr) 82px;gap:8px;align-items:center}
.automationCommandRow .textInput{min-width:0}
.commandGo{width:100%;min-height:53px;margin:0;border-radius:12px;background:linear-gradient(180deg,#426cff,#284de0);color:#fff;font-size:16px}
.commandGo:hover{background:linear-gradient(180deg,#5079ff,#2b50df)}
.automationEntry{border:1px solid #3e4e5c;border-radius:11px;background:linear-gradient(135deg,rgba(35,46,57,.72),rgba(14,21,27,.9));margin:0 0 8px;padding:10px}
.automationEntry:last-child{margin-bottom:0}
.automationEntryTop{display:flex;align-items:center;justify-content:space-between;gap:8px}
.automationEntryName{font-size:17px;font-weight:650}
.automationEntryState{color:#55df8a;font-size:13px}
.automationEntryText{margin-top:6px;color:#aab4bd;font:13px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,"Liberation Mono",monospace;word-break:break-word;white-space:pre-wrap}
.automationEntryActions{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;margin-top:8px}
.automationEntryActions button{min-height:38px;margin:0;border-radius:9px;font-size:14px}
.automationEntryActions .disableBtn{background:#5b6570}
.automationEntryActions .deleteBtn{background:transparent;color:#dc6262;border:1px solid rgba(220,98,98,.6)}
.automationEntryActions .deleteBtn:hover{color:#ff7777;border-color:#ff7777}
:root[data-theme="light"] .automationEntry{background:linear-gradient(135deg,#f7f9fa,#e9eef2);border-color:#b6c1ca}
:root[data-theme="light"] .automationEntryText{color:#53616c}
@media(max-width:360px){.automationCommandRow{grid-template-columns:1fr}.commandGo{min-height:46px}}
</style>
</head>
<body>
<div id="pageLoader"><div id="pageLoaderBar"></div></div>
<div id="apBanner" style="display:none;position:fixed;top:0;left:0;right:0;z-index:1000;background:linear-gradient(180deg,#f4b942,#e8a42a);color:#2a1d00;padding:9px 14px;font:13px/1.4 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;text-align:center;box-shadow:0 3px 12px rgba(0,0,0,.4)">
  Setup AP active &mdash; connect to <b id="apBannerSsid">…</b> at <b>http://<span id="apBannerApIp">192.168.4.1</span></b><br>
  LAN address: <b id="apBannerStaIp" style="font-family:ui-monospace,Menlo,Consolas,monospace">connecting…</b>
</div>
<svg width="0" height="0" style="position:absolute">
<symbol id="i-grid" viewBox="0 0 24 24"><rect x="3" y="3" width="6" height="6" rx="1"></rect><rect x="15" y="3" width="6" height="6" rx="1"></rect><rect x="3" y="15" width="6" height="6" rx="1"></rect><rect x="15" y="15" width="6" height="6" rx="1"></rect></symbol>
<symbol id="i-settings" viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"></circle><path d="M19.4 15a1.8 1.8 0 0 0 .36 1.98l.06.06-1.7 1.7-.06-.06a1.8 1.8 0 0 0-1.98-.36 1.8 1.8 0 0 0-1.1 1.65V20h-2.4v-.09a1.8 1.8 0 0 0-1.1-1.65 1.8 1.8 0 0 0-1.98.36l-.06.06-1.7-1.7.06-.06A1.8 1.8 0 0 0 8 15a1.8 1.8 0 0 0-1.65-1.1H6v-2.4h.35A1.8 1.8 0 0 0 8 10.4a1.8 1.8 0 0 0-.36-1.98l-.06-.06 1.7-1.7.06.06A1.8 1.8 0 0 0 11.32 7 1.8 1.8 0 0 0 12.4 5.35V5h2.4v.35A1.8 1.8 0 0 0 15.9 7a1.8 1.8 0 0 0 1.98-.36l.06-.06 1.7 1.7-.06.06A1.8 1.8 0 0 0 19.2 10.4 1.8 1.8 0 0 0 20.85 11H21v2.4h-.35A1.8 1.8 0 0 0 19.4 15Z"></path></symbol>
<symbol id="i-info" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"></circle><path d="M12 10v6"></path><path d="M12 7h.01"></path></symbol>
<symbol id="i-upload" viewBox="0 0 24 24"><path d="M12 16V4"></path><path d="m7 9 5-5 5 5"></path><path d="M5 20h14"></path><path d="M6 14v6"></path><path d="M18 14v6"></path></symbol>
<symbol id="i-tv" viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="12" rx="2"></rect><path d="M9 21h6"></path><path d="M12 17v4"></path></symbol>
<symbol id="i-snow" viewBox="0 0 24 24"><path d="M12 2v20M4.9 6.1l14.2 11.8M4.9 17.9 19.1 6.1M7 4.2 17 19.8M17 4.2 7 19.8"></path><path d="M9 3h6M9 21h6M3 9v6M21 9v6"></path></symbol>
<symbol id="i-power" viewBox="0 0 24 24"><path d="M12 3v8"></path><path d="M7.1 5.7a8 8 0 1 0 9.8 0"></path></symbol>
<symbol id="i-volume" viewBox="0 0 24 24"><path d="M4 10v4h4l5 4V6l-5 4H4Z"></path><path d="M17 9.5a4 4 0 0 1 0 5"></path><path d="M19.5 7a7 7 0 0 1 0 10"></path></symbol>
<symbol id="i-volume-minus" viewBox="0 0 24 24"><path d="M4 10v4h4l5 4V6l-5 4H4Z"></path><path d="M17 12h4"></path></symbol>
<symbol id="i-mute" viewBox="0 0 24 24"><path d="M4 10v4h4l5 4V6l-5 4H4Z"></path><path d="m17 9 4 6"></path><path d="m21 9-4 6"></path></symbol>
<symbol id="i-edit" viewBox="0 0 24 24"><path d="m4 16.5-.8 4.3L7.5 20 19 8.5 15.5 5 4 16.5Z"></path><path d="m13.8 6.7 3.5 3.5"></path></symbol>
<symbol id="i-trash" viewBox="0 0 24 24"><path d="M4 7h16"></path><path d="M9 7V4h6v3"></path><path d="M6 7l1 14h10l1-14"></path><path d="M10 11v6M14 11v6"></path></symbol>
<symbol id="i-wifi" viewBox="0 0 24 24"><path d="M2.5 8.5a15 15 0 0 1 19 0"></path><path d="M5.5 12a10 10 0 0 1 13 0"></path><path d="M8.7 15.2a5.5 5.5 0 0 1 6.6 0"></path><path d="M12 19h.01"></path></symbol>
<symbol id="i-globe" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"></circle><path d="M3 12h18"></path><path d="M12 3a14 14 0 0 1 0 18"></path><path d="M12 3a14 14 0 0 0 0 18"></path></symbol>
<symbol id="i-chip" viewBox="0 0 24 24"><rect x="7" y="7" width="10" height="10" rx="2"></rect><path d="M9 2v3M15 2v3M9 19v3M15 19v3M2 9h3M2 15h3M19 9h3M19 15h3"></path></symbol>
<symbol id="i-temp" viewBox="0 0 24 24"><path d="M10 15.5V5a2 2 0 1 1 4 0v10.5a5 5 0 1 1-4 0Z"></path><path d="M12 12v7"></path></symbol>
<symbol id="i-ir" viewBox="0 0 24 24"><path d="M4 18 18 4"></path><path d="m14 4 6 6"></path><path d="M4 20c4-4 12-4 16 0"></path></symbol>
<symbol id="i-save" viewBox="0 0 24 24"><path d="M5 3h12l2 2v16H5V3Z"></path><path d="M8 3v6h8V3"></path><path d="M8 21v-7h8v7"></path></symbol>
<symbol id="i-home" viewBox="0 0 24 24"><path d="m3 11 9-8 9 8"></path><path d="M5 10v10h14V10"></path><path d="M9 20v-6h6v6"></path></symbol>
<symbol id="i-repeat" viewBox="0 0 24 24"><path d="M17 2l4 4-4 4"></path><path d="M3 11V9a3 3 0 0 1 3-3h15"></path><path d="M7 22l-4-4 4-4"></path><path d="M21 13v2a3 3 0 0 1-3 3H3"></path></symbol>
<symbol id="i-database" viewBox="0 0 24 24"><ellipse cx="12" cy="5" rx="7" ry="3"></ellipse><path d="M5 5v7c0 1.7 3.1 3 7 3s7-1.3 7-3V5"></path><path d="M5 12v7c0 1.7 3.1 3 7 3s7-1.3 7-3v-7"></path></symbol>
<symbol id="i-alexa" viewBox="0 0 24 24"><circle cx="8" cy="12" r="2.6"></circle><circle cx="16" cy="8" r="2.6"></circle><circle cx="16" cy="16" r="2.6"></circle></symbol>
<symbol id="i-pulse" viewBox="0 0 24 24"><path d="M2 12h5l2-7 5 14 2-7h6"></path></symbol>
<symbol id="i-settop" viewBox="0 0 24 24"><rect x="2" y="8" width="20" height="9" rx="2"></rect><path d="M6 12.5h5"></path><path d="M17 12.5h.01"></path><path d="M6 20v-3M18 20v-3"></path></symbol>
<symbol id="i-streamer" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"></circle><path d="M10 8.5v7l6-3.5-6-3.5Z"></path></symbol>
<symbol id="i-speaker" viewBox="0 0 24 24"><rect x="6" y="2" width="12" height="20" rx="2"></rect><circle cx="12" cy="15" r="3"></circle><circle cx="12" cy="7" r="1"></circle></symbol>
<symbol id="i-fan" viewBox="0 0 24 24"><circle cx="12" cy="12" r="1.6"></circle><path d="M12 10.5C9 8 9.5 3.5 12 3.5S15 8 12 10.5Z"></path><path d="M12 10.5C9 8 9.5 3.5 12 3.5S15 8 12 10.5Z" transform="rotate(120 12 12)"></path><path d="M12 10.5C9 8 9.5 3.5 12 3.5S15 8 12 10.5Z" transform="rotate(240 12 12)"></path></symbol>
<symbol id="i-light" viewBox="0 0 24 24"><path d="M12 3a6 6 0 0 0-3.5 10.9c.6.5 1 1.2 1 2.1h5c0-.9.4-1.6 1-2.1A6 6 0 0 0 12 3Z"></path><path d="M9.5 18.5h5"></path><path d="M10.5 21h3"></path></symbol>
</svg>

<div class="app">
<section class="page active" id="homePage">
  <div class="homeDeviceRow">
    <div class="homeDeviceName" id="homeDeviceName">Universal IR Hub</div>
        <script>
    (function(){
      try {
        var n = localStorage.getItem('irHubName');
        if (n) document.getElementById('homeDeviceName').textContent = n;
      } catch(e) {}
    })();
    </script>
    <div class="homeTemp" id="homeTemp" title="Temperature"><svg><use href="#i-temp"></use></svg><span id="homeTempValue">—</span></div>
    <button class="themeToggle" id="themeToggle" onclick="toggleTheme()" aria-label="Theme" title="Switch theme"><svg class="themeIcon" id="themeIcon" viewBox="0 0 24 24" aria-hidden="true"></svg></button>
  </div>
  <button class="card clickable" onclick="openDashboard()"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-grid"></use></svg></span><span class="cardLabel">Device Dashboard</span></span><span class="chevron">›</span></button>
  <button class="card clickable" onclick="openAutomations()"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-pulse"></use></svg></span><span class="cardLabel">Automations</span></span><span class="chevron">›</span></button>
  <button class="card clickable" onclick="showPage('configPage')"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-settings"></use></svg></span><span class="cardLabel">Configuration</span></span><span class="chevron">›</span></button>
  <button class="card clickable" onclick="openInfo()"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-info"></use></svg></span><span class="cardLabel">Information</span></span><span class="chevron">›</span></button>
  <button class="card clickable" onclick="openUpgrade()"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-upload"></use></svg></span><span class="cardLabel">Firmware Upgrade</span></span><span class="chevron">›</span></button>  <button class="deleteFull" onclick="confirmAction('Restart the Universal IR Hub?','restart')">↻&nbsp; Restart</button>
  <div class="version" style="text-align:right">Overkill v1.0 by sheminasalam</div>
</section>

<section class="page" id="dashboardPage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('homePage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div><button class="arrangeToggle" id="dashboardArrangeBtn" onclick="toggleArrange('dashboardPage','dashboardArrangeBtn')">Edit</button></div>
  <div class="titleRow"><div class="title">Device Dashboard</div><svg class="pageIcon"><use href="#i-grid"></use></svg></div>
  <div id="dashboardDevices"></div>
  <button class="card actionCard greenAction" onclick="showPage('addRemotePage')"><span><span class="plus">＋</span>Add New Remote</span></button>
  <button class="secondary" style="width:100%;margin-top:12px" onclick="showPage('homePage')"><svg class="icon secondaryIcon"><use href="#i-home"></use></svg>Main Menu</button>
</section>

<section class="page" id="devicePage">
<div class="topbar pageBack"><button class="back" onclick="showPage('dashboardPage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div><button class="arrangeToggle" id="deviceArrangeBtn" onclick="toggleArrange('devicePage','deviceArrangeBtn')">Edit</button></div>  <div class="deviceHeader"><div class="scrollWrap" style="flex:1;min-width:0"><div class="title scrollInner" id="devicePageTitle" style="margin:0">Device</div></div><span class="idBadge" id="deviceHeaderIdBadge"></span><svg class="headerIcon"><use id="deviceHeaderIcon" href="#i-tv"></use></svg><div class="deviceConnection"><span class="dot green"></span>Connected</div></div>
  <div id="deviceCommands"></div>
  <div id="deviceChannels" style="margin-top:12px"></div>
  <button class="card actionCard" onclick="addCommandForCurrent()"><span><span class="plus">＋</span>Add New Command</span></button>
  <div class="secondaryRow">
    <button class="secondary" onclick="showPage('dashboardPage')"><svg class="icon secondaryIcon"><use href="#i-grid"></use></svg>Device Dashboard</button>
    <button class="secondary" onclick="openCurrentDeviceEdit()"><svg class="icon secondaryIcon"><use href="#i-settings"></use></svg>Edit Device</button>
  </div>
  <button class="deleteFull" onclick="confirmDeleteCurrentDevice()"><svg class="icon" style="width:19px;height:19px;vertical-align:-4px"><use href="#i-trash"></use></svg>&nbsp;Delete Remote Device</button>
</section>

<section class="page" id="editDevicePage">
  <div class="topbar pageBack"><button class="back" onclick="returnToCurrentDevice()" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Edit Device</div><svg class="pageIcon"><use href="#i-edit"></use></svg></div>
  <div class="card formCard">
    <div class="formLabel">Device Name</div>
    <div class="inputWithIcon"><input class="textInput" id="deviceNameInput" maxlength="31"><svg class="inputEditIcon"><use href="#i-edit"></use></svg></div>
    <div class="formLabel" style="margin-top:16px">Device Category</div>
    <select class="selectInput" id="editDeviceCategory" onchange="syncCloudToggle()"></select>
    <div class="toggleRow" id="cloudToggleRow">
      <span class="iconCircle" style="width:40px;height:40px;flex-basis:40px"><svg class="icon"><use href="#i-alexa"></use></svg></span>
      <div class="toggleText"><div class="toggleTitle">Alexa Cloud</div><div class="toggleSub" id="cloudStateText">Not exposed to Alexa Cloud</div></div>
      <button class="toggle" id="cloudToggle" onclick="toggleCloudEdit()" aria-label="Toggle Alexa Cloud"></button>
    </div>
    <div class="smallNote" id="cloudDiscoveryWarning" style="display:none;color:#ef6767;margin-top:6px">
      ⚠ Alexa Cloud requires the Custom Integration discovery mode. Change it in Configuration → Configure Hub → Discovery.
    </div>
  </div>
  <button class="saveFull" onclick="saveDeviceName()">✓&nbsp; Save</button>
  <button class="secondary" style="width:100%;margin-top:12px" onclick="returnToCurrentDevice()"><svg class="icon secondaryIcon"><use href="#i-tv"></use></svg>Device</button>
</section>

<section class="page" id="editCommandPage">
  <div class="topbar pageBack"><button class="back" onclick="returnToCurrentDevice()" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Edit Command</div><svg class="pageIcon"><use href="#i-edit"></use></svg></div>
  <div class="card formCard">
    <div style="display:flex;align-items:center;gap:10px;margin:-3px 0 16px"><span class="iconCircle" style="width:40px;height:40px;flex-basis:40px"><svg class="icon"><use id="editCmdIcon" href="#i-ir"></use></svg></span><strong id="editCommandHeading" style="font-size:20px">Command</strong></div>
    <div class="formLabel">Command Name</div>
    <div class="inputWithIcon"><input class="textInput" id="commandNameInput" maxlength="31"><svg class="inputEditIcon"><use href="#i-edit"></use></svg></div>
    <div class="toggleRow">
      <span class="iconCircle" style="width:40px;height:40px;flex-basis:40px"><svg class="icon"><use href="#i-alexa"></use></svg></span>
      <div class="toggleText"><div class="toggleTitle">Alexa Integration</div><div class="toggleSub" id="alexaStateText">Not Exposed to Alexa</div></div>
      <button class="toggle" id="alexaToggle" onclick="toggleAlexaCurrent()" aria-label="Toggle Alexa"></button>
    </div>
    <div class="formLabel" style="margin-top:8px">Relearn</div>
    <button class="relearnBtn" onclick="requestRelearnCurrent()">↻&nbsp; Relearn IR Command</button>
  </div>
  <button class="saveFull" onclick="saveCommandName()">✓&nbsp; Save</button>
  <button class="deleteFull" style="margin-top:12px" onclick="confirmDeleteCurrentCommand()"><svg class="icon" style="width:19px;height:19px;vertical-align:-4px"><use href="#i-trash"></use></svg>&nbsp;Delete Command</button>
  <button class="secondary" style="width:100%;margin-top:12px" onclick="returnToCurrentDevice()"><svg class="icon secondaryIcon"><use href="#i-tv"></use></svg>Device</button>
</section>

<section class="page" id="addRemotePage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('dashboardPage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Add New Remote</div><svg class="pageIcon"><use href="#i-tv"></use></svg></div>
  <div class="card formCard">
    <div class="formLabel">Device Name</div>
    <input class="textInput" id="newDeviceName" maxlength="31" placeholder="Living Room TV">
    <div class="formLabel" style="margin-top:16px">First Command (optional hint)</div>
    <input class="textInput" id="newFirstCommand" maxlength="31" placeholder="Power">
    <label class="checkboxRow"><input class="checkbox" id="newAlexa" type="checkbox">Expose learned command to Alexa</label>
    <div class="formLabel" style="margin-top:16px">Device Category</div>
    <select class="selectInput" id="newDeviceCategory" onchange="if(this.value==='NONE')document.getElementById('newCloud').checked=false"></select>
    <label class="checkboxRow" id="newCloudRow"><input class="checkbox" id="newCloud" type="checkbox">Expose device to Alexa Cloud</label>
    <div class="smallNote" id="newCloudNote">Alexa Cloud needs a category. It is separate from the local Hue option above.</div>
    <div class="smallNote" id="newCloudWarn" style="display:none;color:#ef6767">
      ⚠ Alexa Cloud requires Custom Integration discovery. Change it in Configuration → Configure Hub → Discovery.
    </div>
  </div>
  <div class="smallNote">The command name is confirmed after the learn is complete. The hint above will pre-fill that prompt.</div>
  <button class="saveFull" onclick="startLearnNewRemote()">Start Learn</button>
  <button class="secondary" style="width:100%;margin-top:12px" onclick="showPage('dashboardPage')">Cancel</button>
</section>

<section class="page" id="addCommandPage">
  <div class="topbar pageBack"><button class="back" onclick="returnToCurrentDevice()" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Add New Command</div><svg class="pageIcon"><use href="#i-ir"></use></svg></div>
  <div class="card formCard">
    <div class="formLabel" id="addCommandDeviceLabel">Device</div>
    <div class="smallNote">Tap Start Learn, then press the remote. You will be asked for a name after capture.</div>
  </div>
  <button class="saveFull" onclick="startLearnNewCommand()">Start Learn</button>
  <button class="secondary" style="width:100%;margin-top:12px" onclick="returnToCurrentDevice()">Cancel</button>
</section>

<section class="page" id="relearnConfirmPage">
  <div class="topbar pageBack"><button class="back" onclick="returnToCurrentCommandEdit()" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Relearn Command</div><svg class="pageIcon"><use href="#i-repeat"></use></svg></div>
  <div class="card formCard">
    <div style="font-size:20px;font-weight:650" id="relearnCommandTitle">Command</div>
    <div class="smallNote" id="relearnConfirmText" style="margin-top:10px">The existing learned waveform will be replaced. This cannot be undone.</div>
  </div>
  <button class="deleteFull" style="margin-top:12px" onclick="startRelearnCapture()">Confirm Relearn</button>
  <button class="secondary" style="width:100%;margin-top:12px" onclick="returnToCurrentCommandEdit()">Cancel</button>
</section>

<section class="page" id="automationsPage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('homePage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>  <div class="titleRow"><div class="title">Automations</div><svg class="pageIcon"><use href="#i-pulse"></use></svg></div>
  <div class="card sectionCard">
    <div class="sectionHead"><svg class="icon"><use href="#i-pulse"></use></svg><strong>Rules</strong></div>
    <div class="sectionBody">
      <div class="formLabel">Rule Command</div>
      <div class="automationCommandRow"><input class="textInput" id="ruleCommandInput" placeholder="RULE ADD 1 ON TEMP GT 29 DUR 300 ... DO TX 4 WAIT 3 TX 7" autocomplete="off" spellcheck="false"><button class="commandGo" onclick="submitRuleCommand()">Run</button></div>
      <div class="smallNote">RULE LIST | SHOW &lt;id&gt; | ENABLE &lt;id&gt; | DISABLE &lt;id&gt; | DELETE &lt;id&gt; | ADD &lt;id&gt; [CD s] ON [TEMP op deg DUR s] [TIME hh:mm hh:mm] DO TX &lt;cmd&gt; WAIT &lt;s&gt; ... | ON ...</div>
    </div>
  </div>
  <div class="card sectionCard">
    <div class="sectionHead"><svg class="icon"><use href="#i-pulse"></use></svg><strong>Current Rules</strong></div>
    <div class="sectionBody" id="ruleList"></div>
  </div>
  <div class="card sectionCard">
    <div class="sectionHead"><svg class="icon"><use href="#i-repeat"></use></svg><strong>Schedules</strong></div>
    <div class="sectionBody">
      <div class="formLabel">Schedule Command</div>
      <div class="automationCommandRow"><input class="textInput" id="scheduleCommandInput" placeholder="SCHEDULE ADD ..." autocomplete="off" spellcheck="false"><button class="commandGo" onclick="submitScheduleCommand()">Run</button></div>
      <div class="smallNote">Accepts: SCHEDULE LIST | SCHEDULE SHOW &lt;id&gt; | SCHEDULE ENABLE &lt;id&gt; | SCHEDULE DISABLE &lt;id&gt; | SCHEDULE DELETE &lt;id&gt; | SCHEDULE ADD ...</div>
    </div>
  </div>
  <div class="card sectionCard">
    <div class="sectionHead"><svg class="icon"><use href="#i-repeat"></use></svg><strong>Current Schedules</strong></div>
    <div class="sectionBody" id="scheduleList"></div>
  </div>
  <a class="secondary" style="width:100%;margin-bottom:12px;text-decoration:none;box-sizing:border-box"
     href="https://github.com/sheminasalam/OverKill-Smart-IR-Hub/wiki/Automations"
     target="_blank" rel="noopener noreferrer">
    <svg class="icon secondaryIcon"><use href="#i-globe"></use></svg>
    Automation Help (GitHub)
  </a>
  <button class="secondary" style="width:100%" onclick="showPage('homePage')"><svg class="icon secondaryIcon"><use href="#i-home"></use></svg>Main Menu</button>
</section>

<section class="page" id="configPage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('homePage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Configuration</div><svg class="pageIcon"><use href="#i-settings"></use></svg></div>
  <button class="card clickable" onclick="openWifi()"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-wifi"></use></svg></span><span class="cardLabel">Configure WiFi</span></span><span class="chevron">›</span></button>
  <button class="card clickable" onclick="openMqtt()"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-globe"></use></svg></span><span class="cardLabel">Configure MQTT</span></span><span class="chevron">›</span></button>
  <button class="card clickable" onclick="openHub()"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-chip"></use></svg></span><span class="cardLabel">Configure Hub</span></span><span class="chevron">›</span></button>
  <button class="card clickable" onclick="showPage('huePage')"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-alexa"></use></svg></span><span class="cardLabel">Configure Hue / Alexa</span></span><span class="chevron">›</span></button>
  <button class="card clickable" onclick="openStorage()"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-database"></use></svg></span><span class="cardLabel">Configure Storage</span></span><span class="chevron">›</span></button>
  <button class="card clickable" onclick="openBackup()"><span class="cardMain"><span class="iconCircle"><svg class="icon"><use href="#i-save"></use></svg></span><span class="cardLabel">Backup &amp; Restore</span></span><span class="chevron">›</span></button>  
  <button class="secondary" style="width:100%;margin-top:2px" onclick="showPage('homePage')"><svg class="icon secondaryIcon"><use href="#i-home"></use></svg>Main Menu</button>
</section>

<section class="page" id="hubPage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('configPage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>  <div class="titleRow"><div class="title">Configure Hub</div><svg class="pageIcon"><use href="#i-chip"></use></svg></div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-chip"></use></svg><strong>Hub Name</strong></div>
    <div class="sectionBody"><div class="formLabel">Name</div><div class="inputWithIcon"><input class="textInput" id="hubNameInput" maxlength="31" value="Universal IR Hub"><svg class="inputEditIcon"><use href="#i-edit"></use></svg></div><div class="smallNote">Shown on the homepage.</div></div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-ir"></use></svg><strong>IR Parameters</strong></div>
    <div class="sectionBody">
      <div class="formLabel">IR Receiver GPIO</div><select class="selectInput" id="irRxPin"><option value="1">GPIO1</option><option value="3">GPIO3</option><option value="4">GPIO4</option><option value="5">GPIO5</option><option value="2">GPIO2</option><option value="16">GPIO16</option></select>
      <div class="formLabel" style="margin-top:13px">IR Transmitter GPIO</div><select class="selectInput" id="irTxPin"><option value="4">GPIO4</option><option value="5">GPIO5</option><option value="2">GPIO2</option><option value="1">GPIO1</option><option value="3">GPIO3</option><option value="16">GPIO16</option></select>
      <div class="formLabel" style="margin-top:13px">TX Frequency (Hz)</div><input class="textInput" id="irFreq" type="number" value="38000">
      <div class="formLabel" style="margin-top:13px">TX Repeat Count</div><input class="textInput" id="irRepeat" type="number" value="1">
      <div class="formLabel" style="margin-top:13px">TX Repeat Gap (ms)</div><input class="textInput" id="irGap" type="number" value="100">
    </div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-temp"></use></svg><strong>Temperature Sensor</strong></div>
    <div class="sectionBody">
      <label class="checkboxRow" style="margin-top:0"><input class="checkbox" id="tempEnabled" type="checkbox" checked>Enable Temperature Sensor</label>

      <div class="formLabel" style="margin-top:13px">Sensor GPIO</div>
      <select class="selectInput" id="tempPin"><option value="16">GPIO16</option><option value="2">GPIO2</option><option value="4">GPIO4</option><option value="5">GPIO5</option><option value="1">GPIO1</option><option value="3">GPIO3</option></select>

      <div class="infoRow" style="margin-top:16px"><span>Current Reading</span><span id="tempCurrentReading">—</span></div>
      <div class="infoRow"><span>Current Calibration</span><span id="tempCurrentCal">—</span></div>

      <div class="formLabel" style="margin-top:16px">Reference Thermometer (°C)</div>
      <div style="display:flex;gap:8px;align-items:stretch">
        <input class="textInput" id="tempReference" placeholder="e.g. 22.5" inputmode="decimal" style="flex:1;min-width:0">
        <button type="button" class="commandGo" style="width:auto;padding:0 16px" onclick="autoCalibrateTemp()">Calibrate</button>
      </div>
      <div class="smallNote">Enter the temperature shown on a trusted external thermometer. The hub will set the offset automatically so its reading matches.</div>

      <div class="formLabel" style="margin-top:16px">Manual Calibration Offset (°C)</div>
      <input class="textInput" id="tempCal" value="0.00" inputmode="decimal">

      <div class="formLabel" style="margin-top:13px">Sample Interval (seconds)</div>
      <input class="textInput" id="tempInterval" type="number" value="10">
    </div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-settings"></use></svg><strong>Appearance</strong></div>
    <div class="sectionBody"><div class="formLabel">Theme</div><select class="selectInput" id="themeSelect" onchange="setTheme(this.value)"><option value="dark">Dark</option><option value="light">Light</option></select><div class="smallNote">Use the theme icon on the homepage for a quick switch.</div></div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-pulse"></use></svg><strong>Discovery</strong></div>
    <div class="sectionBody">
      <div class="formLabel">Mode</div>
      <select class="selectInput" id="discoveryMode">
        <option value="custom">Custom Integration (Alexa + HA entities)</option>
        <option value="native">Native HA MQTT (entities only, no Alexa)</option>
      </select>
      <div class="smallNote">Switching clears retained topics from the previous mode. Custom Integration is required for Alexa Cloud.</div>
    </div>
  </div>  
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-pulse"></use></svg><strong>Serial Log</strong></div>
    <div class="sectionBody"><label class="checkboxRow" style="margin-top:0"><input class="checkbox" id="serialLogEnabled" type="checkbox" checked>Enable Serial Log</label><div class="smallNote">Controls SERIAL ON / SERIAL OFF (requires reboot).</div></div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-globe"></use></svg><strong>NTP Server</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>Current Time</span><span id="hubCurrentTime">—</span></div>
      <div class="infoRow"><span>Status</span><span id="hubTimeStatus">—</span></div>    
      <div class="formLabel">NTP Server</div><input class="textInput" id="ntpServer" value="pool.ntp.org" maxlength="63">
      <div class="formLabel" style="margin-top:13px">Time Zone Offset (minutes)</div><input class="textInput" id="ntpTimezone" type="number" value="0" min="-1440" max="1440" step="1">
      <div class="formLabel" style="margin-top:13px">DST Offset (minutes)</div><input class="textInput" id="ntpDst" type="number" value="0" min="0" max="120" step="1">
      <label class="checkboxRow" style="margin-top:10px"><input class="checkbox" id="ntpEnabled" type="checkbox" checked>Enable NTP</label>
    </div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-pulse"></use></svg><strong>Status LED</strong></div>
    <div class="sectionBody">
      <label class="checkboxRow" style="margin-top:0"><input class="checkbox" id="statusLedEnabled" type="checkbox" checked>Enable Status LED</label>
      <div class="formLabel" style="margin-top:13px">Status LED GPIO</div><select class="selectInput" id="statusLedPin"><option value="2">GPIO2</option><option value="1">GPIO1</option><option value="3">GPIO3</option><option value="4">GPIO4</option><option value="5">GPIO5</option></select>
    </div>
  </div>
  <button class="saveFull" onclick="saveHubConfiguration()">✓&nbsp; Save</button>
</section>

<section class="page" id="wifiPage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('configPage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Configure WiFi</div><svg class="pageIcon"><use href="#i-wifi"></use></svg></div>
  <div class="card formCard">
    <div class="formLabel">WiFi Network</div>
    <select class="selectInput" id="wifiNetworks"><option>(click Scan)</option></select>
    <button class="scanBtn" onclick="scanWifi()">Scan for Networks</button>
    <div class="scanStatus" id="wifiScanStatus">Select a detected network or scan again.</div>
    <div class="formLabel" style="margin-top:16px">WiFi Password</div>
    <div style="position:relative">
     <input class="textInput" id="wifiPassword" type="password" placeholder="Leave blank to keep current" style="padding-right:78px">
     <button type="button" id="wifiPasswordToggle" onclick="toggleWifiPassword()" style="position:absolute;right:8px;top:50%;transform:translateY(-50%);min-height:34px;padding:0 14px;border-radius:8px;background:linear-gradient(180deg,#3a4651,#232c34);border:1px solid #4e5c69;color:#cfd6dc;font-size:14px">Show</button>
    </div>
    <div class="formLabel" style="margin-top:16px">Hostname</div>
    <input class="textInput" id="wifiHostname" maxlength="32">
<label class="checkboxRow" style="margin-top:16px">
  <input class="checkbox" id="staticIpEnabled" type="checkbox">Use Static IP
</label>
<div id="staticIpFields" style="display:none">
  <div class="formLabel" style="margin-top:13px">IP Address</div>
  <input class="textInput" id="staticIpAddress" placeholder="192.168.1.50" maxlength="15">
  <div class="formLabel" style="margin-top:13px">Gateway</div>
  <input class="textInput" id="staticIpGateway" placeholder="192.168.1.1" maxlength="15">
  <div class="formLabel" style="margin-top:13px">Subnet Mask</div>
  <input class="textInput" id="staticIpSubnet" placeholder="255.255.255.0" maxlength="15">
  <div class="formLabel" style="margin-top:13px">DNS (optional)</div>
  <input class="textInput" id="staticIpDns" placeholder="leave blank for gateway" maxlength="15">
</div>    
  </div>
  <button class="saveFull" onclick="saveWifi()">✓&nbsp; Save</button>
</section>

<section class="page" id="mqttPage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('configPage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Configure MQTT</div><svg class="pageIcon"><use href="#i-globe"></use></svg></div>
  <div class="card formCard">
    <label class="checkboxRow" style="margin-top:0"><input class="checkbox" id="mqttEnabled" type="checkbox" checked>Enable MQTT</label>
    <div class="formLabel" style="margin-top:16px">Broker</div><input class="textInput" id="mqttBroker" maxlength="63">
    <div class="formLabel" style="margin-top:16px">Port</div><input class="textInput" id="mqttPort" type="number" value="1883">
    <div class="formLabel" style="margin-top:16px">User</div><input class="textInput" id="mqttUser" maxlength="63">
    <div class="formLabel" style="margin-top:16px">Password</div>
    <div style="position:relative">
     <input class="textInput" id="mqttPassword" type="password" placeholder="Leave blank to keep current" style="padding-right:78px">
     <button type="button" id="mqttPasswordToggle" onclick="toggleMqttPassword()" style="position:absolute;right:8px;top:50%;transform:translateY(-50%);min-height:34px;padding:0 14px;border-radius:8px;background:linear-gradient(180deg,#3a4651,#232c34);border:1px solid #4e5c69;color:#cfd6dc;font-size:14px">Show</button>
    </div><div class="formLabel" style="margin-top:16px">Client ID</div><input class="textInput" id="mqttClientId" maxlength="31">
    <div class="formLabel" style="margin-top:16px">Base Topic</div><input class="textInput" id="mqttBaseTopic" maxlength="95">
    <label class="checkboxRow"><input class="checkbox" id="mqttHaDiscovery" type="checkbox" checked>Enable Home Assistant Auto Discovery</label>
  </div>
  <button class="saveFull" onclick="saveMqtt()">✓&nbsp; Save</button>
</section>

<section class="page" id="huePage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('configPage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Configure Hue / Alexa</div><svg class="pageIcon"><use href="#i-alexa"></use></svg></div>
  <div class="card sectionCard">
    <div class="sectionHead"><svg class="icon"><use href="#i-alexa"></use></svg><strong>Emulated Hue</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>HTTP</span><span id="hueHttpState" class="ok">—</span></div>
      <div class="infoRow"><span>HTTP Port</span><span>80</span></div>
      <div class="infoRow"><span>SSDP</span><span>1900 UDP</span></div>
    </div>
  </div>
  <div class="smallNote">Local Hue exposure is set per command (Edit Command). Alexa Cloud exposure is set per device (Edit Device).</div>
</section>

<section class="page" id="storagePage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('configPage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Configure Storage</div><svg class="pageIcon"><use href="#i-database"></use></svg></div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-database"></use></svg><strong>W25Qxx Database</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>Detected</span><span id="stDetected">—</span></div>
      <div class="infoRow"><span>Capacity</span><span id="stCapacity">—</span></div>
      <div class="infoRow"><span>Generation</span><span id="stGeneration">—</span></div>
      <div class="infoRow"><span>Used Sectors</span><span id="stUsed">—</span></div>
      <div class="infoRow"><span>Free Sectors</span><span id="stFree">—</span></div>
      <div class="infoRow"><span>Database</span><span id="stHealthy">—</span></div>
    </div>
  </div>
  <button class="secondary" style="width:100%" onclick="loadStorage()">Refresh</button>
  <button class="deleteFull" onclick="confirmAction('Reset the database? All learned data will be removed.','reset')">Reset Database</button>
  <button class="deleteFull" onclick="confirmAction('Wipe the entire W25Qxx database?','wipe')">Wipe Database</button>
</section>

<section class="page" id="backupPage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('configPage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Backup &amp; Restore</div><svg class="pageIcon"><use href="#i-save"></use></svg></div>

  <div class="card sectionCard">
    <div class="sectionHead"><svg class="icon"><use href="#i-save"></use></svg><strong>Back Up</strong></div>
    <div class="sectionBody">
      <div class="smallNote" style="margin-top:0">Saves WiFi, MQTT, hub name, pins, static IP, NTP and temperature calibration. Learned IR codes, rules and schedules live on the W25Qxx database and are unaffected.</div>
      <button class="secondary" style="width:100%;margin-top:12px" onclick="backupToFlash()"><svg class="icon secondaryIcon"><use href="#i-database"></use></svg>Backup to Flash</button>
      <button class="secondary" style="width:100%;margin-top:8px" onclick="downloadBackup()"><svg class="icon secondaryIcon"><use href="#i-save"></use></svg>Download Backup</button>
      <div class="smallNote" id="backupStatus" style="margin-top:10px"></div>
    </div>
  </div>

  <div class="card sectionCard">
    <div class="sectionHead"><svg class="icon"><use href="#i-repeat"></use></svg><strong>Restore</strong></div>
    <div class="sectionBody">
      <button class="deleteFull" style="margin-top:0" onclick="restoreFromFlash()"><svg class="icon" style="width:19px;height:19px;vertical-align:-4px"><use href="#i-database"></use></svg>&nbsp;Restore from Flash</button>
      <div class="formLabel" style="margin-top:16px">Restore from File (.bin)</div>
      <input class="textInput" id="restoreFile" type="file" accept=".bin" style="padding-top:13px">
      <button class="deleteFull" style="margin-top:12px" onclick="restoreFromFile()"><svg class="icon" style="width:19px;height:19px;vertical-align:-4px"><use href="#i-upload"></use></svg>&nbsp;Restore from File</button>
      <div class="smallNote">The device will reboot after restore.</div>
    </div>
  </div>

  <button class="secondary" style="width:100%" onclick="showPage('configPage')"><svg class="icon secondaryIcon"><use href="#i-home"></use></svg>Back</button>
</section>

<section class="page" id="infoPage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('homePage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Information</div><svg class="pageIcon"><use href="#i-info"></use></svg></div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-chip"></use></svg><strong>Firmware</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>Version</span><span id="infoFirmware">—</span></div>
      <div class="infoRow"><span>IR Library</span><span id="infoIrLib">—</span></div>
      <div class="infoRow"><span>Free Heap</span><span id="infoHeap">—</span></div>
      <div class="infoRow"><span>Uptime</span><span id="infoUptime">—</span></div>
    </div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-database"></use></svg><strong>Remote Database</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>Total Devices</span><span id="infoDevices">—</span></div>
      <div class="infoRow"><span>Total Commands</span><span id="infoCommands">—</span></div>
      <div class="infoRow"><span>Alexa Exposed</span><span id="infoAlexa">—</span></div>
    </div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-wifi"></use></svg><strong>WiFi</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>SSID</span><span id="infoWifiSsid">—</span></div>
      <div class="infoRow"><span>State</span><span id="infoWifiState">—</span></div>
      <div class="infoRow"><span>IP</span><span id="infoWifiIp">—</span></div>
      <div class="infoRow"><span>RSSI</span><span id="infoWifiRssi">—</span></div>
    </div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-globe"></use></svg><strong>MQTT</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>State</span><span id="infoMqttState">—</span></div>
      <div class="infoRow"><span>Broker</span><span id="infoMqttBroker">—</span></div>
      <div class="infoRow"><span>Port</span><span id="infoMqttPort">—</span></div>
    </div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-ir"></use></svg><strong>IR Engine</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>State</span><span id="infoIrState">—</span></div>
      <div class="infoRow"><span>RX</span><span id="infoIrRx">—</span></div>
      <div class="infoRow"><span>TX</span><span id="infoIrTx">—</span></div>
      <div class="infoRow"><span>Frequency</span><span id="infoIrFreq">—</span></div>
    </div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-database"></use></svg><strong>Storage</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>Capacity</span><span id="infoFlashCapacity">—</span></div>
      <div class="infoRow"><span>Database</span><span id="infoFlashState">—</span></div>
    </div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-temp"></use></svg><strong>Temperature</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>Reading</span><span id="infoTempC">—</span></div>
    </div>
  </div>
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-globe"></use></svg><strong>Time / NTP</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>Local Time</span><span id="infoTimeLocal">—</span></div>
      <div class="infoRow"><span>UTC Time</span><span id="infoTimeUtc">—</span></div>
      <div class="infoRow"><span>NTP Server</span><span id="infoTimeServer">—</span></div>
      <div class="infoRow"><span>State</span><span id="infoTimeStatus">—</span></div>
    </div>
  </div>  
  <div class="card sectionCard"><div class="sectionHead"><svg class="icon"><use href="#i-chip"></use></svg><strong>Developer</strong></div>
    <div class="sectionBody">
      <div class="infoRow"><span>Author</span><span>sheminasalam</span></div>
      <div class="infoRow"><span>Project</span><span>OverKill Smart IR Hub</span></div>
      <div class="infoRow"><span>Version</span><span>v1.0</span></div>
      <a class="secondary" style="width:100%;margin-top:12px;text-decoration:none;box-sizing:border-box" href="https://github.com/sheminasalam/OverKill-Smart-IR-Hub" target="_blank" rel="noopener noreferrer">
        <svg class="icon secondaryIcon"><use href="#i-globe"></use></svg>
        GitHub Repository
      </a>
    </div>
  </div>  
  <button class="secondary" style="width:100%" onclick="showPage('homePage')"><svg class="icon secondaryIcon"><use href="#i-home"></use></svg>Main Menu</button>
</section>

<section class="page" id="upgradePage">
  <div class="topbar pageBack"><button class="back" onclick="showPage('homePage')" aria-label="Back"><svg class="backIcon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 4 7 12l8 8"></path></svg></button><div class="eyebrow">UNIVERSAL IR HUB</div></div>
  <div class="titleRow"><div class="title">Firmware Upgrade</div><svg class="pageIcon"><use href="#i-upload"></use></svg></div>
  <div class="card formCard">
    <div class="formLabel">Current Version</div>
    <input class="textInput" id="fwCurrent" disabled>
    <div class="formLabel" style="margin-top:16px">Firmware File (.bin)</div>
    <input class="textInput" id="fwFile" type="file" accept=".bin" style="padding-top:13px">
  </div>
  <div class="smallNote">Upload the compiled .bin from Arduino IDE: Sketch &rarr; Export Compiled Binary. The device will reboot when the upload finishes.</div>
  <button class="saveFull" id="fwUpgradeBtn" onclick="startUpgrade()">⇪&nbsp; Start Upgrade</button>
  <div id="fwProgressWrap" style="display:none;margin-top:14px">
    <div class="learnProgressWrap" style="height:8px">
      <div class="learnProgressBar" id="fwProgressBar" style="transition:width .15s linear"></div>
    </div>
    <div class="smallNote" id="fwStatus" style="margin-top:6px;text-align:center">Preparing...</div>
  </div>
</section>

</div>

<div class="modal" id="learnModal"><div class="modalBox">
  <div class="modalTitle" id="learnModalTitle">Learn IR Command</div>
  <div id="learnWaiting">
    <div class="learnVisual"><div class="learnOrb"><svg class="icon"><use href="#i-ir"></use></svg></div></div>
    <div class="learnText">Learning started.<br>Point the remote at the hub and press the required button.</div>
    <div class="learnProgressWrap"><div class="learnProgressBar" id="learnProgressBar"></div></div>
    <button class="fullBtn redOutline" onclick="cancelLearn()">Cancel Learn</button>
  </div>
  <div id="learnNaming" style="display:none">
    <div class="learnVisual"><div class="learnOrb"><svg class="icon"><use href="#i-ir"></use></svg></div></div>
    <div class="learnText">Signal captured!<br>Enter a name for this command.</div>
    <input class="textInput" id="learnNameInput" maxlength="31" placeholder="e.g. Turbo" style="margin-bottom:10px">
    <button class="fullBtn" style="background:linear-gradient(180deg,#25cc70,#11ab58);color:#fff" onclick="saveLearnName()">Save Command</button>
    <button class="fullBtn redOutline" style="margin-top:8px" onclick="cancelLearn()">Discard</button>
  </div>
  <div id="learnSuccess" style="display:none">
    <div class="learnSuccess">✓&nbsp; Learn Successful</div>
    <div class="learnText" id="learnSuccessText">IR command captured successfully.</div>
    <button class="fullBtn" style="background:linear-gradient(180deg,#25cc70,#11ab58);color:#fff" onclick="finishLearn()">Continue</button>
  </div>
</div></div>

<div class="modal" id="confirmModal"><div class="modalBox">
  <div class="modalTitle">Confirm</div>
  <div class="learnText" id="confirmText"></div>
  <div class="modalActions"><button class="redOutline" onclick="closeConfirm()">Cancel</button><button class="saveFull" style="margin:0;min-height:48px" onclick="runConfirmedAction()">Confirm</button></div>
</div></div>

<div class="modal" id="storageModal"><div class="modalBox">
  <div class="modalTitle" id="storageModalTitle">Storage Operation</div>
  <div class="learnVisual"><div class="learnOrb"><svg class="icon"><use href="#i-database"></use></svg></div></div>
  <div class="learnProgressWrap" style="height:12px">
    <div class="learnProgressBar" id="storageProgressBar" style="transition:width .25s linear"></div>
  </div>
  <div style="text-align:center;font-size:30px;font-weight:700;margin:8px 0" id="storagePercent">0%</div>
  <div class="learnText" id="storageMessage">Starting...</div>
</div></div>

<script>
var hubName='Universal IR Hub';
var devices=[];
var currentDeviceId=null;
var currentCommandId=null;
var currentDevice=null;
var currentCommand=null;
var currentAction=null;
var learnPoll=null;
var automationRules=[];
var automationSchedules=[];
var learnAlertShown = false;

window.addEventListener('resize', function(){
  ['dashboardPage','devicePage'].forEach(function(pid){
    var page = document.getElementById(pid);
    if (page && page.classList.contains('active')) setupScrollNames(page);
  });
});

// ── v1.0: temperature calibration helpers ──────────────────
function fmtCenti(v) {
  if (v === null || v === undefined) return '—';
  var neg = v < 0;
  var a = Math.abs(v);
  return (neg ? '-' : '') + Math.floor(a / 100) + '.' +
         String(Math.floor(a % 100)).padStart(2, '0') + ' °C';
}

function updateTempCalibrationUI(t) {
  var r = document.getElementById('tempCurrentReading');
  var k = document.getElementById('tempCurrentCal');
  if (!r || !k) return;
  if (t && t.valid && typeof t.centi_c === 'number') {
    r.textContent = fmtCenti(t.centi_c);
    r.className = 'ok';
  } else if (t && t.enabled) {
    r.textContent = t.connected ? 'waiting…' : 'not detected';
    r.className = 'redText';
  } else {
    r.textContent = 'sensor disabled';
    r.className = 'redText';
  }
  k.textContent = fmtCenti(t ? t.calibration_centi : 0);
}

function autoCalibrateTemp() {
  var refStr = (document.getElementById('tempReference').value || '').trim();
  if (!refStr) { alert('Enter a reference temperature first.'); return; }
  var ref = parseFloat(refStr.replace(',', '.'));
  if (!isFinite(ref) || ref < -100 || ref > 150) {
    alert('Reference must be a number between -100 and 150 °C.');
    return;
  }
  var refCenti = Math.round(ref * 100);
  api('/api/web/config/temp_calibrate', {
    method:'POST',
    body:JSON.stringify({ reference_centi: refCenti })
  }).then(function(r){
    var cal = (r.new_calibration_centi / 100).toFixed(2);
    alert('Calibration updated.\nNew offset: ' + cal + ' °C\nDisplayed reading is now ' +
          (r.new_reading_centi / 100).toFixed(2) + ' °C.');
    document.getElementById('tempReference').value = '';
    return api('/api/web/status').then(function(s){
      updateTempCalibrationUI(s.temperature);
      document.getElementById('tempCal').value =
        (s.temperature.calibration_centi / 100).toFixed(2);
    });
  }).catch(function(e){
    alert('Calibrate failed: ' + e.message);
  });
}

function api(path,opts){
  var merged = Object.assign(
    {headers:{'Content-Type':'application/json'}, cache:'no-store'},
    opts||{}
  );
  return fetch(path, merged).then(function(r){
    if(!r.ok){ return r.text().then(function(t){ throw new Error(r.status+' '+t); }); }
    return r.status === 204 ? null : r.json();
  });
}

// ---- Page loading indicator -------------------------------
var pageLoaderTimer = null;
var pageLoaderHideTimer = null;

function showPageLoader(){
  var el = document.getElementById('pageLoader');
  var bar = document.getElementById('pageLoaderBar');
  if (!el || !bar) return;

  if (pageLoaderHideTimer) { clearTimeout(pageLoaderHideTimer); pageLoaderHideTimer = null; }
  el.classList.add('active');

  // Reset to 0 without animation, then animate up to 70%.
  bar.style.transition = 'none';
  bar.style.width = '0%';
  void bar.offsetWidth;
  bar.style.transition = '';

  bar.style.width = '70%';

  // Creep from 70% to 95% while we wait.
  if (pageLoaderTimer) clearInterval(pageLoaderTimer);
  var pct = 70;
  pageLoaderTimer = setInterval(function(){
    if (pct < 95) { pct += 1; bar.style.width = pct + '%'; }
  }, 200);
}

function hidePageLoader(){
  var el = document.getElementById('pageLoader');
  var bar = document.getElementById('pageLoaderBar');
  if (!el || !bar) return;

  if (pageLoaderTimer) { clearInterval(pageLoaderTimer); pageLoaderTimer = null; }

  // Snap to 100%, hold briefly, then fade out.
  bar.style.width = '100%';
  pageLoaderHideTimer = setTimeout(function(){
    el.classList.remove('active');
    setTimeout(function(){
      bar.style.transition = 'none';
      bar.style.width = '0%';
      void bar.offsetWidth;
      bar.style.transition = '';
    }, 220);
  }, 180);
}

function showPage(id){document.querySelectorAll('.page').forEach(function(p){p.classList.remove('active');});var el=document.getElementById(id);if(el)el.classList.add('active');window.scrollTo(0,0);}
function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,function(c){return({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'})[c];});}
function iconSvg(id,extra){return '<svg class="icon"'+(extra?' '+extra:'')+'><use href="#'+id+'"></use></svg>';}

// ---- Command-line passthrough -----------------------------
// Every state-changing UI action goes through this: exactly the
// line you would type on the serial console.
function runCmd(line){
  return api('/api/web/cmd',{method:'POST',body:JSON.stringify({line:line})})
    .then(function(r){ if(!r || r.ok!==true) throw new Error((r&&r.error)||'command failed'); return r; });
}

function setTheme(t){if(t!=='light')t='dark';document.documentElement.setAttribute('data-theme',t);var sel=document.getElementById('themeSelect');if(sel)sel.value=t;var ic=document.getElementById('themeIcon');if(ic){if(t==='dark'){ic.innerHTML='<circle cx="12" cy="12" r="4"></circle><path d="M12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M19.1 4.9l-1.4 1.4M6.3 17.7l-1.4 1.4"></path>';}else{ic.innerHTML='<path d="M20 15.4A8 8 0 1 1 8.6 4 6.2 6.2 0 0 0 20 15.4Z"></path>';}}try{localStorage.setItem('irHubTheme',t);}catch(e){}}
function toggleTheme(){var c=document.documentElement.getAttribute('data-theme')||'dark';setTheme(c==='dark'?'light':'dark');}
function loadTheme(){var t='dark';try{var s=localStorage.getItem('irHubTheme');if(s==='light'||s==='dark')t=s;}catch(e){}setTheme(t);}
function updateHomeTemp(s){
  var el  = document.getElementById('homeTemp');
  var val = document.getElementById('homeTempValue');
  if (!el || !val) return;
  var t = s && s.temperature;
  var numeric = t && t.valid === true && typeof t.centi_c === 'number';
  if (numeric) {
    val.textContent = (t.centi_c / 100).toFixed(1) + ' °C';
    el.classList.add('visible');
  } else {
    el.classList.remove('visible');
  }
}

// ── v1.0: setup-AP banner ─────────────────────────────────
// While the recovery AP is running, show the AP SSID and the LAN address
// the hub got from your home router, so you never have to hunt for the IP.
function updateApBanner(s){
  var banner = document.getElementById('apBanner');
  if (!banner) return;
  var ap = s && s.wifi && s.wifi.ap;
  if (!ap || !ap.active) {
    banner.style.display = 'none';
    return;
  }
  banner.style.display = 'block';
  var ssidEl  = document.getElementById('apBannerSsid');
  var apIpEl  = document.getElementById('apBannerApIp');
  var staEl   = document.getElementById('apBannerStaIp');
  if (ssidEl) ssidEl.textContent = ap.ssid || '—';
  if (apIpEl) apIpEl.textContent = ap.ip || '192.168.4.1';
  if (staEl) {
    staEl.textContent = (s.wifi.connected && s.wifi.ip)
      ? s.wifi.ip
      : 'connecting…';
  }
}function applyHubName(){
  document.title = hubName;
  document.getElementById('homeDeviceName').textContent = hubName;
  document.querySelectorAll('.page .eyebrow').forEach(function(e){e.textContent=hubName.toUpperCase();});
  var i = document.getElementById('hubNameInput');
  if (i) i.value = hubName;
  try { localStorage.setItem('irHubName', hubName); } catch(e) {}
}

var CATEGORY_ICONS={TV:'i-tv',SETTOP:'i-settop',STREAMER:'i-streamer',SPEAKER:'i-speaker',FAN:'i-fan',AC:'i-snow',LIGHT:'i-light',SWITCH:'i-power'};
function categoryIcon(cat){return CATEGORY_ICONS[cat]||'i-tv';}
function loadDevices(){return api('/api/web/devices').then(function(d){devices=d||[];});}
function renderDashboard(){
  var root=document.getElementById('dashboardDevices');
  root.innerHTML='';
  devices.forEach(function(d,idx){
    var wrap=document.createElement('div');
    wrap.style.display='flex';
    wrap.style.alignItems='center';
    wrap.style.gap='6px';

    var b=document.createElement('button');
    b.className='card clickable';
    b.style.flex='1';
    b.innerHTML='<span class="cardMain"><span class="iconCircle">'+iconSvg(categoryIcon(d.category))+'</span><span class="scrollWrap"><span class="scrollInner cardLabel"></span></span><span class="idBadge"></span></span><span class="chevron">›</span>';
    b.querySelector('.cardLabel').textContent=d.name;
    b.querySelector('.idBadge').textContent='ID '+d.id;
    b.onclick=function(){openDevice(d.id);};

    var arrows=document.createElement('div');
    arrows.className='orderBtns';
    var up=document.createElement('button'); up.textContent='▲';
    var dn=document.createElement('button'); dn.textContent='▼';
    up.disabled = (idx===0);
    dn.disabled = (idx===devices.length-1);
    up.onclick=function(e){ e.stopPropagation(); moveDevice(idx,-1); };
    dn.onclick=function(e){ e.stopPropagation(); moveDevice(idx,+1); };
    arrows.appendChild(up); arrows.appendChild(dn);

    wrap.appendChild(b); wrap.appendChild(arrows);
    root.appendChild(wrap);
  });
  setupScrollNames(root);
}

function toggleArrange(pageId, btnId){
  var page = document.getElementById(pageId);
  var btn  = document.getElementById(btnId);
  if (!page || !btn) return;
  var on = page.classList.toggle('arrange-mode');
  btn.classList.toggle('on', on);
  btn.textContent = on ? 'Done' : 'Edit';
  setupScrollNames(page);   // re-measure in BOTH directions
}

function setupScrollNames(root){
  if (!root) return;
  // Double-defer so the measurement runs after layout, even when the
  // page was display:none a moment ago. The 60ms timeout gives mobile
  // Safari and Chrome time to complete layout and font substitution.
  requestAnimationFrame(function(){
    requestAnimationFrame(function(){
      setTimeout(function(){
        root.querySelectorAll('.scrollWrap').forEach(function(wrap){
          var inner = wrap.querySelector('.scrollInner');
          if (!inner) return;

          inner.classList.remove('scrolling');
          inner.style.removeProperty('--scroll-max');
          wrap.classList.remove('fade');

          var overflow = inner.scrollWidth - wrap.clientWidth;
          if (overflow > 8) {
            inner.style.setProperty('--scroll-max', overflow + 'px');
            inner.classList.add('scrolling');
            wrap.classList.add('fade');
          }
        });
      }, 60);
    });
  });
}

function moveDevice(idx,dir){
  var j=idx+dir;
  if(j<0||j>=devices.length)return;
  var arr=devices.slice();
  var t=arr[idx]; arr[idx]=arr[j]; arr[j]=t;
  devices=arr;
  renderDashboard();
  var ids=arr.map(function(d){return d.id;});
  api('/api/web/devices/order',{method:'POST',body:JSON.stringify({device_ids:ids})})
    .catch(function(e){alert('Order save failed: '+e.message);});
}
function openDashboard(){
  showPageLoader();
  var page = document.getElementById('dashboardPage');
  var btn  = document.getElementById('dashboardArrangeBtn');
  if (page) page.classList.remove('arrange-mode');
  if (btn) { btn.classList.remove('on'); btn.textContent = 'Edit'; }
  loadDevices().then(function(){
    renderDashboard();
    showPage('dashboardPage');
    hidePageLoader();
  }).catch(function(e){
    hidePageLoader();
    alert('Load failed: '+e.message);
  });
}

function openDevice(id){
  showPageLoader();
  var page = document.getElementById('devicePage');
  var btn  = document.getElementById('deviceArrangeBtn');
  if (page) page.classList.remove('arrange-mode');
  if (btn) { btn.classList.remove('on'); btn.textContent = 'Edit'; }
  currentDeviceId=id;currentCommandId=null;
  api('/api/web/devices/'+id).then(function(d){
    currentDevice=d;
    document.getElementById('devicePageTitle').textContent=d.name;
    document.getElementById('deviceHeaderIcon').setAttribute('href','#'+categoryIcon(d.category));
    var db = document.getElementById('deviceHeaderIdBadge');
    if (db) db.textContent='ID '+d.id;
    renderDeviceCommands();
    showPage('devicePage');
    setupScrollNames(document.getElementById('deviceCommands'));
    hidePageLoader();
  }).catch(function(e){
    hidePageLoader();
    alert('Open failed: '+e.message);
  });
}

function renderDeviceCommands(){ renderDeviceChannels();
  if(!currentDevice)return;
  var root=document.getElementById('deviceCommands');
  root.innerHTML='';
  var cmds=currentDevice.commands||[];
  if(!cmds.length){root.innerHTML='<div class="empty">No learned commands.</div>';return;}
  var plain=cmds.filter(function(x){return !x.ch;});
  cmds.forEach(function(c){
    if(c.ch)return;
    var cidx=plain.indexOf(c);
    var card=document.createElement('div');
    card.className='card commandCard';
    card.innerHTML='<div class="commandTop"><div class="commandIdentity"><span class="commandIcon">'+iconSvg('i-ir')+'</span><span class="scrollWrap"><span class="scrollInner commandName"></span></span><span class="idBadge"></span></div><div class="commandActions"><button class="pillBtn pressBtn">⌁&nbsp; Press</button><button class="pillBtn editBtn" aria-label="Edit">'+iconSvg('i-edit')+'</button><span class="orderBtns"><button class="cmdUp">▲</button><button class="cmdDn">▼</button></span></div></div>';
    card.querySelector('.commandName').textContent=c.name;
    card.querySelector('.idBadge').textContent='CMD '+c.id;
    card.querySelector('.pressBtn').onclick=function(){api('/api/web/command/press',{method:'POST',body:JSON.stringify({id:c.id})}).catch(function(e){alert('Press failed: '+e.message);});};
    card.querySelector('.editBtn').onclick=function(){openEditCommand(c.id);};
    var up=card.querySelector('.cmdUp');
    var dn=card.querySelector('.cmdDn');
    up.disabled = (cidx===0);
    dn.disabled = (cidx===plain.length-1);
    up.onclick=function(){ moveCommandById(c.id,-1,false); };
    dn.onclick=function(){ moveCommandById(c.id,+1,false); };
    root.appendChild(card);
  });
  setupScrollNames(root);
}

var channelFormOpen=false,channelEditId=0,CHANNEL_LIMIT=10;
function renderDeviceChannels(){
  var root=document.getElementById('deviceChannels');
  if(!root)return;
  root.innerHTML='';
  root.style.marginTop='0';
  if(!currentDevice)return;
  var cat=currentDevice.category||'';
  if(cat!=='TV'&&cat!=='SETTOP')return;
  var chans=(currentDevice.commands||[]).filter(function(c){return !!c.ch;});

  chans.forEach(function(c,ci){
    var card=document.createElement('div');
    card.className='card commandCard';
    card.innerHTML='<div class="commandTop"><div class="commandIdentity"><span class="commandIcon">'+iconSvg('i-tv')+'</span><span class="scrollWrap"><span class="scrollInner commandName"></span></span><span class="idBadge"></span></div><div class="commandActions"><button class="pillBtn pressBtn">⌁&nbsp; Press</button><button class="pillBtn editBtn" aria-label="Edit">'+iconSvg('i-edit')+'</button><span class="orderBtns"><button class="chUp">▲</button><button class="chDn">▼</button></span></div></div>';
    card.querySelector('.commandName').textContent=c.name+'  ·  '+c.ch;
    card.querySelector('.idBadge').textContent='CMD '+c.id;
    card.querySelector('.pressBtn').onclick=function(){
      api('/api/web/command/press',{method:'POST',body:JSON.stringify({id:c.id})}).catch(function(e){alert('Press failed: '+e.message);});
    };
    card.querySelector('.editBtn').onclick=function(){
      channelEditId=c.id;channelFormOpen=true;renderDeviceChannels();
    };
    var cu=card.querySelector('.chUp'),cd=card.querySelector('.chDn');
    cu.disabled=(ci===0);
    cd.disabled=(ci===chans.length-1);
    cu.onclick=function(){moveCommandById(c.id,-1,true);};
    cd.onclick=function(){moveCommandById(c.id,+1,true);};
    root.appendChild(card);
  });

  if(!channelFormOpen){
    if(chans.length>=CHANNEL_LIMIT){
      var full=document.createElement('div');
      full.className='smallNote';
      full.style.marginBottom='12px';
      full.textContent='Channel limit reached ('+CHANNEL_LIMIT+' per device). Delete one to add another.';
      root.appendChild(full);
      setupScrollNames(root);
      return;
    }
    var add=document.createElement('button');
    add.className='card actionCard';
    add.innerHTML='<span><span class="plus">＋</span>Add New Channel</span>';
    add.onclick=function(){channelEditId=0;channelFormOpen=true;renderDeviceChannels();};
    root.appendChild(add);
    setupScrollNames(root);
    return;
  }

  var cur=channelEditId?chans.filter(function(x){return x.id===channelEditId;})[0]:null;
  if(channelEditId&&!cur)channelEditId=0;

  var form=document.createElement('div');
  form.className='card formCard';
  form.innerHTML=
    '<div class="formLabel">Channel Name</div>'+
    '<input class="textInput chName" maxlength="31" placeholder="e.g. Asianet">'+
    '<div class="formLabel" style="margin-top:16px">Channel Number</div>'+
    '<input class="textInput chNum" inputmode="numeric" maxlength="6" placeholder="e.g. 22">'+
    '<label class="checkboxRow"><input class="checkbox chAlexa" type="checkbox">Expose to Alexa</label>'+
    '<button class="saveFull chSave">✓&nbsp; '+(cur?'Save Channel':'Add Channel')+'</button>'+
    (cur?'<button class="deleteFull chDelete">Delete Channel</button>':'')+
    '<button class="secondary chCancel" style="width:100%;margin-top:12px">Cancel</button>';

  var nameIn=form.querySelector('.chName'),numIn=form.querySelector('.chNum'),alexaIn=form.querySelector('.chAlexa');
  if(cur){nameIn.value=cur.name;numIn.value=cur.ch;alexaIn.checked=!!cur.alexa;}

  form.querySelector('.chCancel').onclick=function(){channelEditId=0;channelFormOpen=false;renderDeviceChannels();};

  if(cur){
    form.querySelector('.chDelete').onclick=function(){
      if(!confirm('Delete channel "'+cur.name+'"?'))return;
      runCmd('DELETE COMMAND '+cur.id).then(function(){
        channelEditId=0;channelFormOpen=false;openDevice(currentDevice.id);
      }).catch(function(e){alert('Failed: '+e.message);});
    };
  }

  form.querySelector('.chSave').onclick=function(){
    var n=nameIn.value.trim(),num=numIn.value.trim();
    if(!n){alert('Enter a channel name.');return;}
    if(!/^[0-9]{1,6}$/.test(num)){alert('Number must be 1 to 6 digits.');return;}
    var addFn=function(){return api('/api/web/channel',{method:'POST',body:JSON.stringify({device_name:currentDevice.name,name:n,number:num,alexa:alexaIn.checked})});};
    var p=channelEditId?runCmd('DELETE COMMAND '+channelEditId).then(addFn):addFn();
    p.then(function(){channelEditId=0;channelFormOpen=false;openDevice(currentDevice.id);})
     .catch(function(e){alert('Save failed: '+e.message);channelEditId=0;channelFormOpen=false;openDevice(currentDevice.id);});
  };

  root.appendChild(form);
  setupScrollNames(root);
}

function moveCommandById(id,dir,isCh){
  if(!currentDevice||!currentDevice.commands)return;
  var arr=currentDevice.commands.slice();
  var idxs=[],p=-1;
  for(var i=0;i<arr.length;i++){
    if(!!arr[i].ch===isCh){ if(arr[i].id===id)p=idxs.length; idxs.push(i); }
  }
  var q=p+dir;
  if(p<0||q<0||q>=idxs.length)return;
  var t=arr[idxs[p]]; arr[idxs[p]]=arr[idxs[q]]; arr[idxs[q]]=t;
  currentDevice.commands=arr;
  renderDeviceCommands();
  var ids=arr.map(function(c){return c.id;});
  api('/api/web/device/order',{method:'POST',body:JSON.stringify({id:currentDevice.id,command_ids:ids})})
    .catch(function(e){alert('Order save failed: '+e.message);});
}
function moveCommand(idx,dir){
  if(!currentDevice||!currentDevice.commands)return;
  var j=idx+dir;
  if(j<0||j>=currentDevice.commands.length)return;
  var arr=currentDevice.commands.slice();
  var t=arr[idx]; arr[idx]=arr[j]; arr[j]=t;
  currentDevice.commands=arr;
  renderDeviceCommands();
  var ids=arr.map(function(c){return c.id;});
  api('/api/web/device/order',{method:'POST',body:JSON.stringify({id:currentDevice.id,command_ids:ids})})
    .catch(function(e){alert('Order save failed: '+e.message);});
}
function returnToCurrentDevice(){if(currentDeviceId)openDevice(currentDeviceId);else showPage('dashboardPage');}
var editCloudOn=false;
function openCurrentDeviceEdit(){
  if(!currentDevice)return;
  document.getElementById('deviceNameInput').value=currentDevice.name;
  document.getElementById('editDeviceCategory').value=currentDevice.category||'NONE';
  editCloudOn=!!currentDevice.cloud;
  updateCloudUI();
  showPage('editDevicePage');
}
function updateCloudUI(){
  var custom = (window.hubDiscoveryMode === 'custom');
  var toggle = document.getElementById('cloudToggle');
  var row = document.getElementById('cloudToggleRow');
  var warn = document.getElementById('cloudDiscoveryWarning');

  toggle.classList.toggle('on', editCloudOn && custom);
  toggle.disabled = !custom;
  if (row) row.style.opacity = custom ? '1' : '0.45';
  if (warn) warn.style.display = custom ? 'none' : 'block';

  if (!custom) {
    document.getElementById('cloudStateText').textContent = 'Unavailable in Native HA mode';
  } else {
    document.getElementById('cloudStateText').textContent =
      editCloudOn ? 'Exposed to Alexa Cloud' : 'Not exposed to Alexa Cloud';
  }
}
function toggleCloudEdit(){
  if (window.hubDiscoveryMode !== 'custom') {
    alert('Alexa Cloud requires Custom Integration discovery. Change it in Configuration → Configure Hub → Discovery.');
    return;
  }
  if(!editCloudOn&&document.getElementById('editDeviceCategory').value==='NONE'){alert('Pick a device category first.');return;}
  editCloudOn=!editCloudOn;updateCloudUI();
}
function syncCloudToggle(){
  if(document.getElementById('editDeviceCategory').value==='NONE'&&editCloudOn){editCloudOn=false;updateCloudUI();}
}function saveDeviceName(){
  if(!currentDevice)return;
  var v=document.getElementById('deviceNameInput').value.trim();
  if(!v){alert('Device name cannot be empty.');return;}
  var cat=document.getElementById('editDeviceCategory').value;
  var cloud=editCloudOn;
  if (cloud && window.hubDiscoveryMode !== 'custom') {
    alert('Alexa Cloud requires Custom Integration discovery.');
    return;
  }
  if(cloud&&cat==='NONE'){alert('Pick a device category to use Alexa Cloud.');return;}
  var p=(v!==currentDevice.name)?runCmd('RENAME DEVICE '+currentDevice.id+' '+v):Promise.resolve();
  p.then(function(){
    return api('/api/web/device/settings',{method:'POST',body:JSON.stringify({id:currentDevice.id,category:cat,cloud:cloud})});
  }).then(function(){
    currentDevice.name=v;
    loadDevices().then(function(){openDevice(currentDevice.id);});
  }).catch(function(e){alert('Save failed: '+e.message);});
}

function openEditCommand(cid){
  showPageLoader();
  api('/api/web/commands/'+cid).then(function(cmd){
    currentCommand=cmd;
    currentCommandId=cid;
    document.getElementById('editCommandHeading').textContent=cmd.name;
    document.getElementById('commandNameInput').value=cmd.name;
    updateAlexaUI(cmd.alexa);
    showPage('editCommandPage');
    hidePageLoader();
  }).catch(function(e){
    hidePageLoader();
    alert('Load failed: '+e.message);
  });
}function updateAlexaUI(on){var t=document.getElementById('alexaToggle');var s=document.getElementById('alexaStateText');t.classList.toggle('on',!!on);s.textContent=on?'Exposed to Alexa':'Not Exposed to Alexa';}
function toggleAlexaCurrent(){if(!currentCommand)return;var next=!currentCommand.alexa;api('/api/web/command/alexa',{method:'POST',body:JSON.stringify({id:currentCommand.id,enabled:next})}).then(function(){currentCommand.alexa=next;updateAlexaUI(next);}).catch(function(e){alert('Save failed: '+e.message);});}
function saveCommandName(){if(!currentCommand)return;var v=document.getElementById('commandNameInput').value.trim();if(!v){alert('Command name cannot be empty.');return;}runCmd('RENAME COMMAND '+currentCommand.id+' '+v).then(function(){currentCommand.name=v;returnToCurrentDevice();}).catch(function(e){alert('Rename failed: '+e.message);});}
function returnToCurrentCommandEdit(){showPage('editCommandPage');}

function requestRelearnCurrent(){if(!currentCommand)return;document.getElementById('relearnCommandTitle').textContent=currentCommand.name;document.getElementById('relearnConfirmText').textContent='The existing learned waveform for "'+currentCommand.name+'" will be replaced. This cannot be undone.';showPage('relearnConfirmPage');}
function startRelearnCapture(){if(!currentCommand)return;startLearnFlow({mode:'relearn',command_id:currentCommand.id},'Relearn IR Command',currentCommand.name);}

function addCommandForCurrent(){if(!currentDevice)return;document.getElementById('addCommandDeviceLabel').textContent=currentDevice.name;showPage('addCommandPage');}
function startLearnNewCommand(){if(!currentDevice){alert('No device selected.');return;}startLearnFlow({mode:'new_command',device_name:currentDevice.name},'Learn New Command','');}
var DEVICE_CATEGORIES=[['NONE','Not set'],['TV','TV'],['SETTOP','Set-top box'],['STREAMER','Streaming device'],['SPEAKER','Speaker / soundbar'],['FAN','Fan'],['AC','Air conditioner'],['LIGHT','Light'],['SWITCH','Switch / generic']];
function fillCategorySelects(){
  ['newDeviceCategory','editDeviceCategory'].forEach(function(id){
    var s=document.getElementById(id);if(!s)return;
    s.innerHTML='';
    DEVICE_CATEGORIES.forEach(function(c){var o=document.createElement('option');o.value=c[0];o.textContent=c[1];s.appendChild(o);});
  });
}
fillCategorySelects();
function startLearnNewRemote(){
  var dn=document.getElementById('newDeviceName').value.trim();
  var cn=document.getElementById('newFirstCommand').value.trim();
  var cat=document.getElementById('newDeviceCategory').value;
  var cloud=document.getElementById('newCloud').checked;
  if (cloud && window.hubDiscoveryMode !== 'custom') {
    alert('Alexa Cloud requires Custom Integration discovery.');
    return;
  }
  if(!dn){alert('Device name is required.');return;}
  if(cloud&&cat==='NONE'){alert('Pick a device category to use Alexa Cloud.');return;}
  var payload={mode:'new_command',device_name:dn};
  if(cat!=='NONE'){payload.category=cat;payload.cloud=cloud;}
  startLearnFlow(payload,'Learn New Remote',cn);
}

function startUpgrade(){
  var fileInput = document.getElementById('fwFile');
  var btn       = document.getElementById('fwUpgradeBtn');
  var wrap      = document.getElementById('fwProgressWrap');
  var bar       = document.getElementById('fwProgressBar');
  var status    = document.getElementById('fwStatus');

  if (!fileInput.files || !fileInput.files.length) {
    alert('Please select a .bin file first.');
    return;
  }
  var file = fileInput.files[0];
  if (!/\.bin$/i.test(file.name)) {
    alert('Firmware must be a .bin file.');
    return;
  }

  var url = 'http://' + location.hostname + ':8080/update';
  var form = new FormData();
  form.append('firmware', file, file.name);

  wrap.style.display = 'block';
  bar.style.width = '0%';
  status.textContent = 'Preparing device...';
  btn.disabled = true;
  btn.textContent = 'Uploading...';
  window.__otaActive = true;

  fetch('/api/web/ota/prepare', {method:'POST'})
    .then(function(){ return fetch('/api/web/ota/prepare', {method:'POST'}); })
    .catch(function(){})
    .then(function(){
      return new Promise(function(resolve){ setTimeout(resolve, 500); });
    })
    .then(function(){
      status.textContent = 'Uploading ' + (file.size / 1024).toFixed(0) + ' KB...';

      var xhr = new XMLHttpRequest();
      xhr.open('POST', url, true);

      xhr.upload.onprogress = function(e){
        if (!e.lengthComputable) return;
        var pct = Math.round((e.loaded / e.total) * 100);
        bar.style.width = pct + '%';
        status.textContent = 'Uploading... ' + pct + '%';
      };

      xhr.onload = function(){
        window.__otaActive = false;
        btn.disabled = false;
        btn.textContent = '⇪  Start Upgrade';
        if (xhr.status === 200) {
          bar.style.width = '100%';
          status.textContent = 'Upload complete. Device is rebooting...';
          setTimeout(function(){
            alert('Firmware updated. The device will be unreachable for about 10 seconds, then reload this page.');
          }, 300);
        } else {
          status.textContent = 'Upload failed: HTTP ' + xhr.status;
          alert('Upload failed: HTTP ' + xhr.status);
        }
      };

      xhr.onerror = function(){
        window.__otaActive = false;
        btn.disabled = false;
        btn.textContent = '⇪  Start Upgrade';
        status.textContent = 'Connection lost during upload. The device may still be flashing.';
      };

      xhr.send(form);
    });
}

function startLearnProgress(){
  var bar=document.getElementById('learnProgressBar');
  if(!bar)return;
  bar.classList.remove('fast');
  bar.classList.remove('done');
  bar.style.transition='none';
  bar.style.width='0%';
  void bar.offsetWidth;             // force reflow so the reset is applied
  bar.style.transition='';          // back to the 12 s linear transition
  bar.style.width='90%';            // animate up to 90% and hold there
}
function finishLearnProgress(){
  var bar=document.getElementById('learnProgressBar');
  if(!bar)return;
  bar.classList.add('fast');        // switch to the short ease-out transition
  bar.classList.add('done');        // turn green
  bar.style.width='100%';
}
function resetLearnProgress(){
  var bar=document.getElementById('learnProgressBar');
  if(!bar)return;
  bar.classList.add('fast');
  bar.classList.remove('done');
  bar.style.width='0%';
}

function startLearnFlow(payload,title,defaultName){
  learnAlertShown = false;    //
  api('/api/web/learn/begin',{method:'POST',body:JSON.stringify(payload)}).then(function(){
    document.getElementById('learnModalTitle').textContent=title;
    document.getElementById('learnWaiting').style.display='block';
    document.getElementById('learnNaming').style.display='none';
    document.getElementById('learnSuccess').style.display='none';
    document.getElementById('learnNameInput').value=defaultName||'';
    window.__learnDefaultName=defaultName||'';
    document.getElementById('learnModal').classList.add('show');
    startLearnProgress();    if(learnPoll)clearInterval(learnPoll);
    learnPoll=setInterval(pollLearn,600);
  }).catch(function(e){alert('Learn start failed: '+e.message);});
}
function pollLearn(){
  api('/api/web/learn/status').then(function(s){
    if(s.state==='AWAITING_NAME'){
      if(learnPoll){clearInterval(learnPoll);learnPoll=null;}
      document.getElementById('learnWaiting').style.display='none';
      document.getElementById('learnNaming').style.display='block';
      if(!document.getElementById('learnNameInput').value){
        document.getElementById('learnNameInput').value=window.__learnDefaultName||'';
      }
      setTimeout(function(){document.getElementById('learnNameInput').focus();},50);
    } else if(s.state==='SUCCESS'){
      if(learnPoll){clearInterval(learnPoll);learnPoll=null;}
      document.getElementById('learnWaiting').style.display='none';
      document.getElementById('learnNaming').style.display='none';
      document.getElementById('learnSuccess').style.display='block';
      document.getElementById('learnSuccessText').textContent=s.message||'IR command captured successfully.';
      window.__learnResult={deviceId:s.current_device_id,commandId:s.current_command_id};
    } else if(s.state==='FAILED'){
      if(learnPoll){clearInterval(learnPoll);learnPoll=null;}
      if(learnAlertShown) return;
      learnAlertShown = true;
      document.getElementById('learnModal').classList.remove('show');
      api('/api/web/learn/cancel',{method:'POST'}).catch(function(){});
      resetLearnProgress();
      setTimeout(function(){
        alert('Learn failed: ' + (s.message || 'timeout'));
      }, 50);
    } else if(s.state==='IDLE'){
      if(learnPoll){clearInterval(learnPoll);learnPoll=null;}
      document.getElementById('learnModal').classList.remove('show');
    }
  }).catch(function(e){});
}
function saveLearnName(){
  var n=document.getElementById('learnNameInput').value.trim();
  if(!n){alert('Name is required.');return;}
  api('/api/web/learn/save',{method:'POST',body:JSON.stringify({name:n})}).then(function(){
    if(learnPoll)clearInterval(learnPoll);
    learnPoll=setInterval(pollLearn,400);
  }).catch(function(e){alert('Save failed: '+e.message);});
}
function cancelLearn(){
  api('/api/web/learn/cancel',{method:'POST'}).catch(function(){});
  if(learnPoll){clearInterval(learnPoll);learnPoll=null;}
  resetLearnProgress();
  document.getElementById('learnModal').classList.remove('show');
}
function finishLearn(){
  document.getElementById('learnModal').classList.remove('show');
  api('/api/web/learn/cancel',{method:'POST'}).catch(function(){});
  var r=window.__learnResult||{};
  if(r.deviceId)openDevice(r.deviceId);
  else if(currentDeviceId)openDevice(currentDeviceId);
  window.__learnResult=null;
}

function confirmDeleteCurrentCommand(){if(currentCommand)confirmAction('Delete command "'+currentCommand.name+'"?','deleteCommand');}
function confirmDeleteCurrentDevice(){if(currentDevice)confirmAction('Delete remote device "'+currentDevice.name+'"? All commands will be removed.','deleteDevice');}
function confirmAction(text,action){currentAction={action:action};document.getElementById('confirmText').textContent=text;document.getElementById('confirmModal').classList.add('show');}
function closeConfirm(){document.getElementById('confirmModal').classList.remove('show');currentAction=null;}
function runConfirmedAction(){var a=currentAction;closeConfirm();if(!a)return;
  if(a.action==='restart'){runCmd('REBOOT').catch(function(e){});return;}
if(a.action==='reset'){ startStorageJob('reset'); return; }
if(a.action==='wipe') { startStorageJob('wipe');  return; }
  if(a.action==='deleteCommand'){runCmd('DELETE COMMAND '+currentCommandId).then(function(){returnToCurrentDevice();}).catch(function(e){alert('Failed: '+e.message);});return;}
  if(a.action==='deleteDevice'){runCmd('DELETE DEVICE '+currentDeviceId).then(function(){currentDeviceId=null;currentDevice=null;openDashboard();}).catch(function(e){alert('Failed: '+e.message);});return;}
  if(a.action==='deleteAutomation'){
    var cmd=(a.automationType==='rule'?'RULE DELETE ':'SCHEDULE DELETE ')+a.automationId;
    runCmd(cmd).then(loadAutomations).catch(function(e){alert('Failed: '+e.message);});
    return;
  }
}

function loadAutomations(){Promise.all([api('/api/web/rules'),api('/api/web/schedules')]).then(function(r){automationRules=r[0].rules||[];automationSchedules=r[1].schedules||[];renderAutomationLists();}).catch(function(e){automationRules=[];automationSchedules=[];renderAutomationLists();});}
var TEMP_OPS=['','GT','GE','LT','LE','EQ'];
function fmtClock(m){return String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');}
function triggerToCmd(g){
  var p=['ON'];
  if(g.use_interval)p.push('INT',g.interval_s);
  if(g.use_temp){p.push('TEMP',TEMP_OPS[g.temp_op],(g.temp_centi/100).toFixed(2));if(g.temp_dur_s)p.push('DUR',g.temp_dur_s);}
  if(g.use_time)p.push('TIME',fmtClock(g.time_start_min),fmtClock(g.time_end_min));
  if(g.weekday_mask)p.push('WD',g.weekday_mask);
  if(g.use_temp&&g.use_time)p.push('LOG',g.logic?'OR':'AND');
  p.push('DO');
  g.steps.forEach(function(s){p.push(s.t==='tx'?'TX':'WAIT',s.v);});
  return p.join(' ');
}
function ruleToCmd(r){return 'RULE ADD '+r.id+' CD '+(r.cooldown_s||0)+' '+r.triggers.map(triggerToCmd).join(' | ');}
function renderAutomationLists(){
  var rl=document.getElementById('ruleList');rl.innerHTML='';
  if(!automationRules.length)rl.innerHTML='<div class="empty">No rules configured.</div>';
  automationRules.forEach(function(r){
    var row=document.createElement('div');row.className='automationEntry';
    row.innerHTML='<div class="automationEntryTop"><div class="automationEntryName">Rule '+r.id+'</div><div class="automationEntryState">'+(r.enabled?'Enabled':'Disabled')+'</div></div><div class="automationEntryText"></div><div class="automationEntryActions"><button class="disableBtn">'+(r.enabled?'Disable':'Enable')+'</button><button class="mutedBtn showBtn">Edit</button><button class="deleteBtn">Delete</button></div>';
    var summary='CD '+(r.cooldown_s||0)+'s \u00B7 '+r.triggers.length+' trigger'+(r.triggers.length===1?'':'s')+'\n';
    r.triggers.forEach(function(g,i){summary+='#'+(i+1)+' '+triggerToCmd(g).replace(/^ON /,'')+'\n';});
    row.querySelector('.automationEntryText').textContent=summary;
    row.querySelector('.disableBtn').onclick=function(){runCmd('RULE '+(r.enabled?'DISABLE ':'ENABLE ')+r.id).then(loadAutomations).catch(function(e){alert(e.message);});};
    row.querySelector('.showBtn').onclick=function(){var i=document.getElementById('ruleCommandInput');i.value=ruleToCmd(r);i.focus();};
    row.querySelector('.deleteBtn').onclick=function(){confirmAutomationDelete('Delete Rule '+r.id+'?','rule',r.id);};
    rl.appendChild(row);
  });

  var sl=document.getElementById('scheduleList');sl.innerHTML='';
  if(!automationSchedules.length)sl.innerHTML='<div class="empty">No schedules configured.</div>';
  automationSchedules.forEach(function(s){
    var row=document.createElement('div');row.className='automationEntry';
    row.innerHTML='<div class="automationEntryTop"><div class="automationEntryName">Schedule '+s.id+'</div><div class="automationEntryState">'+(s.enabled?'Enabled':'Disabled')+'</div></div><div class="automationEntryText"></div><div class="automationEntryActions"><button class="disableBtn">'+(s.enabled?'Disable':'Enable')+'</button><button class="mutedBtn showBtn">Show</button><button class="deleteBtn">Delete</button></div>';
    row.querySelector('.automationEntryText').textContent='CMD '+s.command_id+' at '+String(s.hour).padStart(2,'0')+':'+String(s.minute).padStart(2,'0')+(s.delay_s?(' DLY '+s.delay_s):'');
    row.querySelector('.disableBtn').onclick=function(){runCmd('SCHEDULE '+(s.enabled?'DISABLE ':'ENABLE ')+s.id).then(loadAutomations).catch(function(e){alert(e.message);});};
    row.querySelector('.showBtn').onclick=function(){document.getElementById('scheduleCommandInput').value='SCHEDULE SHOW '+s.id;};
    row.querySelector('.deleteBtn').onclick=function(){confirmAutomationDelete('Delete Schedule '+s.id+'?','schedule',s.id);};
    sl.appendChild(row);
  });
}
function openAutomations(){
  showPageLoader();
  loadAutomations();
  showPage('automationsPage');
  hidePageLoader();
}function submitRuleCommand(){var v=document.getElementById('ruleCommandInput').value.trim();if(!v)return;runCmd(v).then(function(){document.getElementById('ruleCommandInput').value='';loadAutomations();}).catch(function(e){alert('Rule command failed: '+e.message);});}
function submitScheduleCommand(){var v=document.getElementById('scheduleCommandInput').value.trim();if(!v)return;runCmd(v).then(function(){document.getElementById('scheduleCommandInput').value='';loadAutomations();}).catch(function(e){alert('Schedule command failed: '+e.message);});}
function confirmAutomationDelete(text,type,id){currentAction={action:'deleteAutomation',automationType:type,automationId:id};document.getElementById('confirmText').textContent=text;document.getElementById('confirmModal').classList.add('show');}

function openWifi(){
  showPageLoader();
  api('/api/web/status').then(function(s){
    document.getElementById('wifiNetworks').innerHTML='<option value="'+esc(s.wifi.ssid||'')+'">'+esc(s.wifi.ssid||'(current)')+'</option>';
    document.getElementById('wifiHostname').value='';
var st = s.wifi.static_ip || {};
document.getElementById('staticIpEnabled').checked = !!st.enabled;
document.getElementById('staticIpAddress').value   = st.ip      || '';
document.getElementById('staticIpGateway').value   = st.gateway || '';
document.getElementById('staticIpSubnet').value    = st.subnet  || '';
document.getElementById('staticIpDns').value       = st.dns     || '';
document.getElementById('staticIpFields').style.display = st.enabled ? 'block' : 'none';    
    document.getElementById('wifiScanStatus').textContent='Select a detected network or scan again.';
    showPage('wifiPage');
    hidePageLoader();
  }).catch(function(e){
    hidePageLoader();
    alert('WiFi open failed: '+e.message);
  });
}
function scanWifi(){
  var statusEl = document.getElementById('wifiScanStatus');
  statusEl.textContent = 'Scanning...';
  var finished = false;

  api('/api/web/config/wifi/scan',{method:'POST'}).then(function(){
    var tries = 0;

    function poll(){
      if (finished) return;
      tries++;
      api('/api/web/config/wifi/scan').then(function(r){
        if (finished) return;

        if (r.state === 'DONE') {
          finished = true;
          var sel = document.getElementById('wifiNetworks');
          sel.innerHTML = '';
          var nets = r.networks || [];
          nets.forEach(function(n){
            var o = document.createElement('option');
            o.value = n.ssid;
            o.textContent = n.ssid + ' (' + n.rssi + ' dBm)' + (n.encrypted ? ' 🔒' : '');
            sel.appendChild(o);
          });
          statusEl.textContent = nets.length + ' networks found.';
          alert('Scan complete: ' + nets.length + ' network' + (nets.length === 1 ? '' : 's') + ' found.');
        } else if (r.state === 'RUNNING') {
          if (tries > 40) {
            finished = true;
            statusEl.textContent = 'Scan timeout.';
            alert('Scan timeout.');
          } else {
            setTimeout(poll, 500);
          }
        } else {
          // IDLE means the server-side result expired before we polled it.
          finished = true;
          statusEl.textContent = 'Scan state reset, please try again.';
        }
      }).catch(function(e){
        if (finished) return;
        finished = true;
        statusEl.textContent = 'Scan failed: ' + e.message;
        alert('Scan failed: ' + e.message);
      });
    }

    setTimeout(poll, 500);
  }).catch(function(e){
    statusEl.textContent = 'Scan start failed: ' + e.message;
    alert('Scan start failed: ' + e.message);
  });
}
function saveWifi(){
  var staticEnabled = document.getElementById('staticIpEnabled').checked;

  var body = {
    ssid:           document.getElementById('wifiNetworks').value,
    password:       document.getElementById('wifiPassword').value,
    hostname:       document.getElementById('wifiHostname').value,
    static_enabled: staticEnabled
  };

  if (staticEnabled) {
    body.static_ip      = document.getElementById('staticIpAddress').value.trim();
    body.static_gateway = document.getElementById('staticIpGateway').value.trim();
    body.static_subnet  = document.getElementById('staticIpSubnet').value.trim();
    body.static_dns     = document.getElementById('staticIpDns').value.trim();
  }

  api('/api/web/config/wifi', {method:'POST', body:JSON.stringify(body)})
    .then(function(r){
      alert('WiFi settings saved.' + (r.warning ? ' (' + r.warning + ')' : ''));
    })
    .catch(function(e){
      alert('Save failed: ' + e.message);
    });
}
function toggleWifiPassword(){
  var inp = document.getElementById('wifiPassword');
  var btn = document.getElementById('wifiPasswordToggle');
  if(!inp || !btn) return;
  if(inp.type === 'password'){ inp.type = 'text'; btn.textContent = 'Hide'; }
  else { inp.type = 'password'; btn.textContent = 'Show'; }
}
function toggleMqttPassword(){
  var inp = document.getElementById('mqttPassword');
  var btn = document.getElementById('mqttPasswordToggle');
  if(!inp || !btn) return;
  if(inp.type === 'password'){ inp.type = 'text'; btn.textContent = 'Hide'; }
  else { inp.type = 'password'; btn.textContent = 'Show'; }
}

function openMqtt(){
  showPageLoader();
  api('/api/web/status').then(function(s){
    document.getElementById('mqttEnabled').checked=s.mqtt.enabled;
    document.getElementById('mqttBroker').value=s.mqtt.broker||'';
    document.getElementById('mqttPort').value=s.mqtt.port||1883;
    document.getElementById('mqttUser').value='';
    document.getElementById('mqttPassword').value='';
    document.getElementById('mqttClientId').value='';
    document.getElementById('mqttBaseTopic').value=s.mqtt.base_topic||'';
    document.getElementById('mqttHaDiscovery').checked=!!s.mqtt.ha_discovery;
    showPage('mqttPage');
    hidePageLoader();
  }).catch(function(e){
    hidePageLoader();
    alert('MQTT open failed: '+e.message);
  });
}
function saveMqtt(){var body={enabled:document.getElementById('mqttEnabled').checked,broker:document.getElementById('mqttBroker').value,port:parseInt(document.getElementById('mqttPort').value,10)||1883,username:document.getElementById('mqttUser').value,password:document.getElementById('mqttPassword').value,client_id:document.getElementById('mqttClientId').value,base_topic:document.getElementById('mqttBaseTopic').value,ha_discovery:document.getElementById('mqttHaDiscovery').checked};api('/api/web/config/mqtt',{method:'POST',body:JSON.stringify(body)}).then(function(r){alert('MQTT settings saved.'+(r.warning?' ('+r.warning+')':''));}).catch(function(e){alert('Save failed: '+e.message);});}

// ── v1.0: temperature calibration helpers ──────────────────
function fmtCenti(v) {
  if (v === null || v === undefined) return '—';
  var neg = v < 0;
  var a = Math.abs(v);
  return (neg ? '-' : '') + Math.floor(a / 100) + '.' +
         String(Math.floor(a % 100)).padStart(2, '0') + ' °C';
}

function updateTempCalibrationUI(t) {
  var r = document.getElementById('tempCurrentReading');
  var k = document.getElementById('tempCurrentCal');
  if (!r || !k) return;
  if (t && t.valid && typeof t.centi_c === 'number') {
    r.textContent = fmtCenti(t.centi_c);
    r.className = 'ok';
  } else if (t && t.enabled) {
    r.textContent = t.connected ? 'waiting…' : 'not detected';
    r.className = 'redText';
  } else {
    r.textContent = 'sensor disabled';
    r.className = 'redText';
  }
  k.textContent = fmtCenti(t ? t.calibration_centi : 0);
}

function autoCalibrateTemp() {
  var refStr = (document.getElementById('tempReference').value || '').trim();
  if (!refStr) { alert('Enter a reference temperature first.'); return; }
  var ref = parseFloat(refStr.replace(',', '.'));
  if (!isFinite(ref) || ref < -100 || ref > 150) {
    alert('Reference must be a number between -100 and 150 °C.');
    return;
  }
  var refCenti = Math.round(ref * 100);

  api('/api/web/config/temp_calibrate', {
    method:'POST',
    body:JSON.stringify({ reference_centi: refCenti })
  }).then(function(r){
    var cal = (r.new_calibration_centi / 100).toFixed(2);
    alert('Calibration updated.\nNew offset: ' + cal + ' °C\nDisplayed reading is now ' +
          (r.new_reading_centi / 100).toFixed(2) + ' °C.');
    document.getElementById('tempReference').value = '';
    // Refetch so the info rows and manual field update.
    return api('/api/web/status').then(function(s){
      updateTempCalibrationUI(s.temperature);
      document.getElementById('tempCal').value =
        (s.temperature.calibration_centi / 100).toFixed(2);
    });
  }).catch(function(e){
    alert('Calibrate failed: ' + e.message);
  });
}

function openHub(){
  showPageLoader();
  api('/api/web/status').then(function(s){
    document.getElementById('hubNameInput').value=s.hub_name||hubName;
    window.hubDiscoveryMode = s.discovery_mode || 'custom';
    document.documentElement.setAttribute('data-discovery-ready','1');
    document.getElementById('discoveryMode').value = window.hubDiscoveryMode;    
    document.getElementById('irFreq').value=s.ir.frequency_hz;
    document.getElementById('irRepeat').value=s.ir.repeat_count;
    document.getElementById('irGap').value=s.ir.repeat_gap_ms;
    document.getElementById('tempEnabled').checked=s.temperature.enabled;
    document.getElementById('tempInterval').value=Math.round(s.temperature.interval_ms/1000);
    document.getElementById('tempCal').value=(s.temperature.calibration_centi/100).toFixed(2);
    updateTempCalibrationUI(s.temperature);
    document.getElementById('serialLogEnabled').checked=s.serial.enabled;
    document.getElementById('ntpEnabled').checked=s.time.enabled;
    document.getElementById('ntpServer').value=s.time.server||'pool.ntp.org';
    document.getElementById('ntpTimezone').value=s.time.gmt_offset_min;
    document.getElementById('ntpDst').value=s.time.dst_offset_min;    var tt = s.time || {};
    document.getElementById('hubCurrentTime').textContent = tt.local || '—';
    document.getElementById('hubTimeStatus').textContent =
      !tt.enabled ? 'Disabled' : tt.valid ? 'Synchronized' : 'Waiting for sync…';

    showPage('hubPage');
    hidePageLoader();
  }).catch(function(e){
    hidePageLoader();
    alert('Hub open failed: '+e.message);
  });
}
function saveHubConfiguration(){
  var name=document.getElementById('hubNameInput').value.trim();
  if(!name){alert('Hub name cannot be empty.');return;}
  var body={
    hub_name:name,
    discovery_mode:document.getElementById('discoveryMode').value,    
    frequency_hz:parseInt(document.getElementById('irFreq').value,10)||38000,
    repeat_count:parseInt(document.getElementById('irRepeat').value,10)||1,
    repeat_gap_ms:parseInt(document.getElementById('irGap').value,10)||100,
    temp_enabled:document.getElementById('tempEnabled').checked,
    temp_interval_ms:(parseInt(document.getElementById('tempInterval').value,10)||10)*1000,
    temp_cal_centi:Math.round(parseFloat(document.getElementById('tempCal').value||'0')*100),
    serial_enabled:document.getElementById('serialLogEnabled').checked,
    ntp_enabled:document.getElementById('ntpEnabled').checked,
    ntp_server:document.getElementById('ntpServer').value,
    ntp_gmt_offset_min:parseInt(document.getElementById('ntpTimezone').value,10)||0,
    ntp_dst_offset_min:parseInt(document.getElementById('ntpDst').value,10)||0
  };
  api('/api/web/config/hub',{method:'POST',body:JSON.stringify(body)}).then(function(r){
    hubName=name;
    applyHubName();
    window.hubDiscoveryMode = document.getElementById('discoveryMode').value;
    document.documentElement.setAttribute('data-discovery-ready','1');
    alert('Configuration saved.'+(r.reboot_required?' Reboot required for pin changes.':''));
  }).catch(function(e){alert('Save failed: '+e.message);});
}

function openUpgrade(){
  showPageLoader();
  showPage('upgradePage');
  var el = document.getElementById('fwCurrent');
  if (el) el.value = 'Loading...';
  api('/api/web/info').then(function(info){
    if (el) el.value = info.firmware || 'unknown';
    hidePageLoader();
  }).catch(function(e){
    if (el) el.value = 'unavailable';
    hidePageLoader();
  });
}
function openInfo(){
  showPageLoader();
  api('/api/web/status').then(function(s){
    api('/api/web/info').then(function(info){
      var set=function(id,v){var e=document.getElementById(id);if(e)e.textContent=v;};
      set('infoFirmware',info.firmware);set('infoIrLib',info.ir_library);
      set('infoHeap',(info.free_heap/1024).toFixed(1)+' KB');set('infoUptime',info.uptime_s+' s');
      set('infoDevices',info.devices);set('infoCommands',info.commands);set('infoAlexa',info.alexa_exposed);
      set('infoWifiSsid', s.wifi.ssid || '—');
      set('infoWifiState',info.wifi_state);set('infoWifiIp',info.wifi_ip||'—');set('infoWifiRssi',(info.wifi_rssi!=null?info.wifi_rssi+' dBm':'—'));
      set('infoMqttState',info.mqtt_state);set('infoMqttBroker',info.mqtt_broker||'—');set('infoMqttPort',info.mqtt_port);
      set('infoIrState',s.ir.state);set('infoIrRx','GPIO'+s.ir.rx_pin);set('infoIrTx','GPIO'+s.ir.tx_pin);set('infoIrFreq',s.ir.frequency_hz+' Hz');
      set('infoFlashState',info.flash_healthy?'HEALTHY':'NOT READY');
      set('infoFlashCapacity',(info.flash_capacity/1024/1024)+' MiB');
      set('infoTempC',info.temp_valid?(info.temp_centi_c/100).toFixed(2)+' °C':'unavailable');
      var tt = s.time || {};
      set('infoTimeLocal', tt.local || '—');
      set('infoTimeUtc',   tt.utc   || '—');
      set('infoTimeServer', tt.server || '—');
      set('infoTimeStatus',
          !tt.enabled ? 'Disabled' :
          tt.valid    ? 'Synchronized' :
                        'Waiting for sync…');      
      document.getElementById('fwCurrent').value=info.firmware;
      document.getElementById('hueHttpState').textContent=s.hue.http_ready?'READY':'STOPPED';
      showPage('infoPage');
      hidePageLoader();
    }).catch(function(e){
      hidePageLoader();
      alert('Info failed: '+e.message);
    });
  }).catch(function(e){
    hidePageLoader();
    alert('Status failed: '+e.message);
  });
}

function openStorage(){
  showPageLoader();
  api('/api/web/status').then(function(s){
    var set=function(id,v){var e=document.getElementById(id);if(e)e.textContent=v;};
    set('stDetected',s.flash.detected?'YES':'NO');
    set('stCapacity',(s.flash.capacity_bytes/1024/1024)+' MiB');
    set('stGeneration',s.flash.generation);
    set('stUsed',s.flash.used_sectors);set('stFree',s.flash.free_sectors);
    set('stHealthy',s.flash.healthy?'HEALTHY':'NOT READY');
    showPage('storagePage');
    hidePageLoader();
  }).catch(function(e){
    hidePageLoader();
    alert('Storage open failed: '+e.message);
  });
}

// ── v1.0: async storage job progress ──────────────────────
var storageJobPoll = null;

function startStorageJob(kind){
  document.getElementById('storageModalTitle').textContent =
    (kind === 'wipe' ? 'Wiping Database' : 'Resetting Database');
  document.getElementById('storageProgressBar').style.width = '0%';
  document.getElementById('storagePercent').textContent = '0%';
  document.getElementById('storageMessage').textContent = 'Starting...';
  document.getElementById('storageModal').classList.add('show');

  api('/api/web/storage/' + kind, {method:'POST'}).then(function(r){
    if (!r || r.ok !== true) throw new Error((r && r.error) || 'start failed');
    if (storageJobPoll) clearInterval(storageJobPoll);
    storageJobPoll = setInterval(pollStorageJob, 500);
  }).catch(function(e){
    document.getElementById('storageMessage').textContent = 'Failed to start: ' + e.message;
    setTimeout(function(){
      document.getElementById('storageModal').classList.remove('show');
      alert('Failed to start: ' + e.message);
    }, 500);
  });
}

function pollStorageJob(){
  api('/api/web/storage/job').then(function(j){
    var pct = j.percent || 0;
    document.getElementById('storageProgressBar').style.width = pct + '%';
    document.getElementById('storagePercent').textContent = pct + '%';
    document.getElementById('storageMessage').textContent = j.message || j.state;

    if (j.state === 'DONE' || j.state === 'FAILED') {
      if (storageJobPoll) { clearInterval(storageJobPoll); storageJobPoll = null; }
      setTimeout(function(){
        document.getElementById('storageModal').classList.remove('show');
        if (j.state === 'DONE') {
          alert(j.message || 'Complete.');
          openStorage();  // refresh the numbers on the page
        } else {
          alert('Operation failed: ' + (j.message || 'unknown error'));
        }
      }, 700);
    }
  }).catch(function(){ /* transient, keep polling */ });
}

function loadStorage(){openStorage();}

function openBackup(){
  document.getElementById('backupStatus').textContent = '';
  var inp = document.getElementById('restoreFile'); if (inp) inp.value = '';
  showPage('backupPage');
}
function downloadBackup(){
  var a = document.createElement('a');
  a.href = '/api/web/config/backup';
  a.download = 'irhub-config.bin';
  document.body.appendChild(a); a.click(); document.body.removeChild(a);
}function backupToFlash(){
  var st = document.getElementById('backupStatus');
  st.textContent = 'Backing up…';
  api('/api/web/config/backup-to-flash', {method:'POST'})
    .then(function(){ st.textContent = 'Saved to flash at ' + new Date().toLocaleTimeString(); })
    .catch(function(e){ st.textContent = 'Failed: ' + e.message; alert('Backup failed: ' + e.message); });
}
function restoreFromFlash(){
  if (!confirm('Restore configuration from flash and reboot?')) return;
  api('/api/web/config/restore-from-flash', {method:'POST'})
    .then(function(){ alert('Restored. Rebooting…'); setTimeout(function(){ location.reload(); }, 10000); })
    .catch(function(e){ alert('Restore failed: ' + e.message); });
}
function restoreFromFile(){
  var inp = document.getElementById('restoreFile');
  if (!inp.files || !inp.files.length) { alert('Select a .bin backup file first.'); return; }
  if (!confirm('Restore configuration from file and reboot?')) return;
  fetch('/api/web/config/restore-from-file', {
    method:'POST',
    headers:{'Content-Type':'application/octet-stream'},
    body: inp.files[0]
  }).then(function(r){
    return r.text().then(function(t){
      var j = null; try { j = JSON.parse(t); } catch(e){}
      if (!r.ok) throw new Error((j && j.error) || ('HTTP ' + r.status));
      return j;
    });
  }).then(function(){ alert('Restored. Rebooting…'); setTimeout(function(){ location.reload(); }, 10000); })
   .catch(function(e){ alert('Restore failed: ' + e.message); });
}

document.addEventListener('DOMContentLoaded', function(){
  window.__otaActive = false;
  // Apply cached name immediately so there's no flash of the default.
  try {
    var cached = localStorage.getItem('irHubName');
    if (cached) {
      hubName = cached;
      applyHubName();
    }
  } catch(e) {}

  loadTheme();
  api('/api/web/status').then(function(s){
    hubName = s.hub_name || hubName;
    window.hubDiscoveryMode = s.discovery_mode || 'custom';
    document.documentElement.setAttribute('data-discovery-ready','1');
    applyHubName();
    updateHomeTemp(s);
    updateApBanner(s);
  }).catch(function(){applyHubName();});

  // Refresh the homepage temperature and AP banner every 30 s while
  // the page is visible. 10 s would be snappier but 30 s is enough
  // for a setup flow.
setInterval(function(){
  if (document.hidden || window.__otaActive) return;
  api('/api/web/status').then(function(s){
    updateHomeTemp(s);
    updateApBanner(s);
  }).catch(function(){});
}, 30000);

  document.addEventListener('visibilitychange', function(){
    if (!document.hidden) {
      api('/api/web/status').then(function(s){
        updateHomeTemp(s);
        updateApBanner(s);
      }).catch(function(){});
    }
  });
  loadDevices().then(renderDashboard).catch(function(){});

  // Firmware file picker
  var fwIn = document.getElementById('fwFile');
  if (fwIn) {
    fwIn.addEventListener('change', function(){
      var st = document.getElementById('fwStatus');
      if (fwIn.files && fwIn.files.length) {
        st.textContent = 'Selected: ' + fwIn.files[0].name +
          ' (' + (fwIn.files[0].size / 1024).toFixed(0) + ' KB)';
        document.getElementById('fwProgressWrap').style.display = 'block';
      }
    });
  }

  // Reflect discovery mode on the Add New Remote cloud checkbox.
  var newCloud = document.getElementById('newCloud');
  if (newCloud) {
    var applyCloudAvail = function(){
      var custom = (window.hubDiscoveryMode === 'custom');
      newCloud.disabled = !custom;
      var row = document.getElementById('newCloudRow');
      if (row) row.style.opacity = custom ? '1' : '0.45';
      var warn = document.getElementById('newCloudWarn');
      if (warn) warn.style.display = custom ? 'none' : 'block';
      if (!custom) newCloud.checked = false;
    };
    applyCloudAvail();
    var mo = new MutationObserver(applyCloudAvail);
    mo.observe(document.documentElement, {attributes:true, attributeFilter:['data-discovery-ready']});
  }

  // ── v1.0: static IP field toggle ────────────────────────
  var staticCb = document.getElementById('staticIpEnabled');
  if (staticCb) {
    staticCb.addEventListener('change', function(){
      document.getElementById('staticIpFields').style.display =
        staticCb.checked ? 'block' : 'none';
    });
  }
  // ────────────────────────────────────────────────────────  
});
</script>
</body>
</html>)=====";