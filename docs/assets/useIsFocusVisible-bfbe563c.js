import{r as s}from"./index-f2bd0723.js";import{r as o}from"./index-0a26bc51.js";var i=!0,n=!1,a=null,l={text:!0,search:!0,url:!0,tel:!0,email:!0,password:!0,number:!0,date:!0,month:!0,week:!0,time:!0,datetime:!0,"datetime-local":!0};function d(e){var t=e.type,r=e.tagName;return!!(r==="INPUT"&&l[t]&&!e.readOnly||r==="TEXTAREA"&&!e.readOnly||e.isContentEditable)}function c(e){e.metaKey||e.altKey||e.ctrlKey||(i=!0)}function u(){i=!1}function f(){this.visibilityState==="hidden"&&n&&(i=!0)}function m(e){e.addEventListener("keydown",c,!0),e.addEventListener("mousedown",u,!0),e.addEventListener("pointerdown",u,!0),e.addEventListener("touchstart",u,!0),e.addEventListener("visibilitychange",f,!0)}function y(e){var t=e.target;try{return t.matches(":focus-visible")}catch{}return i||d(t)}function h(){n=!0,window.clearTimeout(a),a=window.setTimeout(function(){n=!1},100)}function p(){var e=s.useCallback(function(t){var r=o.findDOMNode(t);r!=null&&m(r.ownerDocument)},[]);return{isFocusVisible:y,onBlurVisible:h,ref:e}}export{p as u};
//# sourceMappingURL=useIsFocusVisible-bfbe563c.js.map
