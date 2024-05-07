import{g as at,_ as z,b as st}from"./useTheme-ed20be1d.js";import{_ as M}from"./extends-98964cd2.js";import{R as P,r as m}from"./index-f2bd0723.js";import"./index-e297e3bd.js";import{w as ut,c as lt}from"./capitalize-6c71ac81.js";import{u as dt}from"./useTheme-5d34e5a6.js";import{T as B,u as X}from"./TransitionGroupContext-06ba0be2.js";import{_ as ct}from"./assertThisInitialized-e784747a.js";import{R as w}from"./index-0a26bc51.js";const A={disabled:!1};var ft=function(n){return n.scrollTop},k="unmounted",g="exited",S="entering",R="entered",G="exiting",h=function(l){ct(n,l);function n(r,e){var t;t=l.call(this,r,e)||this;var i=e,o=i&&!i.isMounting?r.enter:r.appear,s;return t.appearStatus=null,r.in?o?(s=g,t.appearStatus=S):s=R:r.unmountOnExit||r.mountOnEnter?s=k:s=g,t.state={status:s},t.nextCallback=null,t}n.getDerivedStateFromProps=function(e,t){var i=e.in;return i&&t.status===k?{status:g}:null};var a=n.prototype;return a.componentDidMount=function(){this.updateStatus(!0,this.appearStatus)},a.componentDidUpdate=function(e){var t=null;if(e!==this.props){var i=this.state.status;this.props.in?i!==S&&i!==R&&(t=S):(i===S||i===R)&&(t=G)}this.updateStatus(!1,t)},a.componentWillUnmount=function(){this.cancelNextCallback()},a.getTimeouts=function(){var e=this.props.timeout,t,i,o;return t=i=o=e,e!=null&&typeof e!="number"&&(t=e.exit,i=e.enter,o=e.appear!==void 0?e.appear:i),{exit:t,enter:i,appear:o}},a.updateStatus=function(e,t){if(e===void 0&&(e=!1),t!==null)if(this.cancelNextCallback(),t===S){if(this.props.unmountOnExit||this.props.mountOnEnter){var i=this.props.nodeRef?this.props.nodeRef.current:w.findDOMNode(this);i&&ft(i)}this.performEnter(e)}else this.performExit();else this.props.unmountOnExit&&this.state.status===g&&this.setState({status:k})},a.performEnter=function(e){var t=this,i=this.props.enter,o=this.context?this.context.isMounting:e,s=this.props.nodeRef?[o]:[w.findDOMNode(this),o],c=s[0],p=s[1],y=this.getTimeouts(),_=o?y.appear:y.enter;if(!e&&!i||A.disabled){this.safeSetState({status:R},function(){t.props.onEntered(c)});return}this.props.onEnter(c,p),this.safeSetState({status:S},function(){t.props.onEntering(c,p),t.onTransitionEnd(_,function(){t.safeSetState({status:R},function(){t.props.onEntered(c,p)})})})},a.performExit=function(){var e=this,t=this.props.exit,i=this.getTimeouts(),o=this.props.nodeRef?void 0:w.findDOMNode(this);if(!t||A.disabled){this.safeSetState({status:g},function(){e.props.onExited(o)});return}this.props.onExit(o),this.safeSetState({status:G},function(){e.props.onExiting(o),e.onTransitionEnd(i.exit,function(){e.safeSetState({status:g},function(){e.props.onExited(o)})})})},a.cancelNextCallback=function(){this.nextCallback!==null&&(this.nextCallback.cancel(),this.nextCallback=null)},a.safeSetState=function(e,t){t=this.setNextCallback(t),this.setState(e,t)},a.setNextCallback=function(e){var t=this,i=!0;return this.nextCallback=function(o){i&&(i=!1,t.nextCallback=null,e(o))},this.nextCallback.cancel=function(){i=!1},this.nextCallback},a.onTransitionEnd=function(e,t){this.setNextCallback(t);var i=this.props.nodeRef?this.props.nodeRef.current:w.findDOMNode(this),o=e==null&&!this.props.addEndListener;if(!i||o){setTimeout(this.nextCallback,0);return}if(this.props.addEndListener){var s=this.props.nodeRef?[this.nextCallback]:[i,this.nextCallback],c=s[0],p=s[1];this.props.addEndListener(c,p)}e!=null&&setTimeout(this.nextCallback,e)},a.render=function(){var e=this.state.status;if(e===k)return null;var t=this.props,i=t.children;t.in,t.mountOnEnter,t.unmountOnExit,t.appear,t.enter,t.exit,t.timeout,t.addEndListener,t.onEnter,t.onEntering,t.onEntered,t.onExit,t.onExiting,t.onExited,t.nodeRef;var o=at(t,["children","in","mountOnEnter","unmountOnExit","appear","enter","exit","timeout","addEndListener","onEnter","onEntering","onEntered","onExit","onExiting","onExited","nodeRef"]);return P.createElement(B.Provider,{value:null},typeof i=="function"?i(e,o):P.cloneElement(P.Children.only(i),o))},n}(P.Component);h.contextType=B;h.propTypes={};function N(){}h.defaultProps={in:!1,mountOnEnter:!1,unmountOnExit:!1,appear:!1,enter:!0,exit:!0,onEnter:N,onEntering:N,onEntered:N,onExit:N,onExiting:N,onExited:N};h.UNMOUNTED=k;h.EXITED=g;h.ENTERING=S;h.ENTERED=R;h.EXITING=G;const pt=h;var Et=function(n){return n.scrollTop};function F(l,n){var a=l.timeout,r=l.style,e=r===void 0?{}:r;return{duration:e.transitionDuration||typeof a=="number"?a:a[n.mode]||0,delay:e.transitionDelay}}var mt=function(n){var a={};return n.shadows.forEach(function(r,e){a["elevation".concat(e)]={boxShadow:r}}),M({root:{backgroundColor:n.palette.background.paper,color:n.palette.text.primary,transition:n.transitions.create("box-shadow")},rounded:{borderRadius:n.shape.borderRadius},outlined:{border:"1px solid ".concat(n.palette.divider)}},a)},ht=m.forwardRef(function(n,a){var r=n.classes,e=n.className,t=n.component,i=t===void 0?"div":t,o=n.square,s=o===void 0?!1:o,c=n.elevation,p=c===void 0?1:c,y=n.variant,_=y===void 0?"elevation":y,D=z(n,["classes","className","component","square","elevation","variant"]);return m.createElement(i,M({className:lt(r.root,e,_==="outlined"?r.outlined:r["elevation".concat(p)],!s&&r.rounded),ref:a},D))});const _t=ut(mt,{name:"MuiPaper"})(ht);function I(l){return"scale(".concat(l,", ").concat(Math.pow(l,2),")")}var vt={entering:{opacity:1,transform:I(1)},entered:{opacity:1,transform:"none"}},J=m.forwardRef(function(n,a){var r=n.children,e=n.disableStrictModeCompat,t=e===void 0?!1:e,i=n.in,o=n.onEnter,s=n.onEntered,c=n.onEntering,p=n.onExit,y=n.onExited,_=n.onExiting,D=n.style,U=n.timeout,T=U===void 0?"auto":U,H=n.TransitionComponent,K=H===void 0?pt:H,Q=z(n,["children","disableStrictModeCompat","in","onEnter","onEntered","onEntering","onExit","onExited","onExiting","style","timeout","TransitionComponent"]),j=m.useRef(),$=m.useRef(),b=dt(),O=b.unstable_strictMode&&!t,L=m.useRef(null),V=X(r.ref,a),Y=X(O?L:void 0,V),C=function(d){return function(E,v){if(d){var f=O?[L.current,E]:[E,v],x=st(f,2),q=x[0],W=x[1];W===void 0?d(q):d(q,W)}}},Z=C(c),tt=C(function(u,d){Et(u);var E=F({style:D,timeout:T},{mode:"enter"}),v=E.duration,f=E.delay,x;T==="auto"?(x=b.transitions.getAutoHeightDuration(u.clientHeight),$.current=x):x=v,u.style.transition=[b.transitions.create("opacity",{duration:x,delay:f}),b.transitions.create("transform",{duration:x*.666,delay:f})].join(","),o&&o(u,d)}),et=C(s),nt=C(_),it=C(function(u){var d=F({style:D,timeout:T},{mode:"exit"}),E=d.duration,v=d.delay,f;T==="auto"?(f=b.transitions.getAutoHeightDuration(u.clientHeight),$.current=f):f=E,u.style.transition=[b.transitions.create("opacity",{duration:f,delay:v}),b.transitions.create("transform",{duration:f*.666,delay:v||f*.333})].join(","),u.style.opacity="0",u.style.transform=I(.75),p&&p(u)}),rt=C(y),ot=function(d,E){var v=O?d:E;T==="auto"&&(j.current=setTimeout(v,$.current||0))};return m.useEffect(function(){return function(){clearTimeout(j.current)}},[]),m.createElement(K,M({appear:!0,in:i,nodeRef:O?L:void 0,onEnter:tt,onEntered:et,onEntering:Z,onExit:it,onExited:rt,onExiting:nt,addEndListener:ot,timeout:T==="auto"?null:T},Q),function(u,d){return m.cloneElement(r,M({style:M({opacity:0,transform:I(.75),visibility:u==="exited"&&!i?"hidden":void 0},vt[u],D,r.props.style),ref:Y},d))})});J.muiSupportAuto=!0;const Dt=J;export{Dt as G,_t as P,pt as T,F as g,Et as r};
//# sourceMappingURL=Grow-ae4bfd44.js.map
