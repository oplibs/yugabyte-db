import{_ as q}from"./extends-98964cd2.js";import{g as Le,_ as pe}from"./useTheme-ed20be1d.js";import{r as t,R as G}from"./index-f2bd0723.js";import"./index-e297e3bd.js";import{r as Ie}from"./index-0a26bc51.js";import{c as J,w as de,_ as Ue}from"./capitalize-6c71ac81.js";import{T as fe,a as A,u as ie}from"./TransitionGroupContext-06ba0be2.js";import{u as ze}from"./useIsFocusVisible-bfbe563c.js";import{_ as Oe,a as Xe}from"./assertThisInitialized-e784747a.js";function oe(i,e){var u=function(a){return e&&t.isValidElement(a)?e(a):a},l=Object.create(null);return i&&t.Children.map(i,function(n){return n}).forEach(function(n){l[n.key]=u(n)}),l}function Ye(i,e){i=i||{},e=e||{};function u(d){return d in e?e[d]:i[d]}var l=Object.create(null),n=[];for(var a in i)a in e?n.length&&(l[a]=n,n=[]):n.push(a);var o,c={};for(var s in e){if(l[s])for(o=0;o<l[s].length;o++){var p=l[s][o];c[l[s][o]]=u(p)}c[s]=u(s)}for(o=0;o<n.length;o++)c[n[o]]=u(n[o]);return c}function B(i,e,u){return u[e]!=null?u[e]:i.props[e]}function je(i,e){return oe(i.children,function(u){return t.cloneElement(u,{onExited:e.bind(null,u),in:!0,appear:B(u,"appear",i),enter:B(u,"enter",i),exit:B(u,"exit",i)})})}function Ae(i,e,u){var l=oe(i.children),n=Ye(e,l);return Object.keys(n).forEach(function(a){var o=n[a];if(t.isValidElement(o)){var c=a in e,s=a in l,p=e[a],d=t.isValidElement(p)&&!p.props.in;s&&(!c||d)?n[a]=t.cloneElement(o,{onExited:u.bind(null,o),in:!0,exit:B(o,"exit",i),enter:B(o,"enter",i)}):!s&&c&&!d?n[a]=t.cloneElement(o,{in:!1}):s&&c&&t.isValidElement(p)&&(n[a]=t.cloneElement(o,{onExited:u.bind(null,o),in:p.props.in,exit:B(o,"exit",i),enter:B(o,"enter",i)}))}}),n}var Ke=Object.values||function(i){return Object.keys(i).map(function(e){return i[e]})},We={component:"div",childFactory:function(e){return e}},ue=function(i){Oe(e,i);function e(l,n){var a;a=i.call(this,l,n)||this;var o=a.handleExited.bind(Xe(a));return a.state={contextValue:{isMounting:!0},handleExited:o,firstRender:!0},a}var u=e.prototype;return u.componentDidMount=function(){this.mounted=!0,this.setState({contextValue:{isMounting:!1}})},u.componentWillUnmount=function(){this.mounted=!1},e.getDerivedStateFromProps=function(n,a){var o=a.children,c=a.handleExited,s=a.firstRender;return{children:s?je(n,c):Ae(n,o,c),firstRender:!1}},u.handleExited=function(n,a){var o=oe(this.props.children);n.key in o||(n.props.onExited&&n.props.onExited(a),this.mounted&&this.setState(function(c){var s=q({},c.children);return delete s[n.key],{children:s}}))},u.render=function(){var n=this.props,a=n.component,o=n.childFactory,c=Le(n,["component","childFactory"]),s=this.state.contextValue,p=Ke(this.state.children).map(o);return delete c.appear,delete c.enter,delete c.exit,a===null?G.createElement(fe.Provider,{value:s},p):G.createElement(fe.Provider,{value:s},G.createElement(a,c,p))},e}(G.Component);ue.propTypes={};ue.defaultProps=We;const He=ue;var Ge=typeof window>"u"?t.useEffect:t.useLayoutEffect;function qe(i){var e=i.classes,u=i.pulsate,l=u===void 0?!1:u,n=i.rippleX,a=i.rippleY,o=i.rippleSize,c=i.in,s=i.onExited,p=s===void 0?function(){}:s,d=i.timeout,E=t.useState(!1),g=E[0],v=E[1],y=J(e.ripple,e.rippleVisible,l&&e.ripplePulsate),h={width:o,height:o,top:-(o/2)+a,left:-(o/2)+n},k=J(e.child,g&&e.childLeaving,l&&e.childPulsate),x=A(p);return Ge(function(){if(!c){v(!0);var M=setTimeout(x,d);return function(){clearTimeout(M)}}},[x,c,d]),t.createElement("span",{className:y,style:h},t.createElement("span",{className:k}))}var ae=550,Je=80,Qe=function(e){return{root:{overflow:"hidden",pointerEvents:"none",position:"absolute",zIndex:0,top:0,right:0,bottom:0,left:0,borderRadius:"inherit"},ripple:{opacity:0,position:"absolute"},rippleVisible:{opacity:.3,transform:"scale(1)",animation:"$enter ".concat(ae,"ms ").concat(e.transitions.easing.easeInOut)},ripplePulsate:{animationDuration:"".concat(e.transitions.duration.shorter,"ms")},child:{opacity:1,display:"block",width:"100%",height:"100%",borderRadius:"50%",backgroundColor:"currentColor"},childLeaving:{opacity:0,animation:"$exit ".concat(ae,"ms ").concat(e.transitions.easing.easeInOut)},childPulsate:{position:"absolute",left:0,top:0,animation:"$pulsate 2500ms ".concat(e.transitions.easing.easeInOut," 200ms infinite")},"@keyframes enter":{"0%":{transform:"scale(0)",opacity:.1},"100%":{transform:"scale(1)",opacity:.3}},"@keyframes exit":{"0%":{opacity:1},"100%":{opacity:0}},"@keyframes pulsate":{"0%":{transform:"scale(1)"},"50%":{transform:"scale(0.92)"},"100%":{transform:"scale(1)"}}}},Ze=t.forwardRef(function(e,u){var l=e.center,n=l===void 0?!1:l,a=e.classes,o=e.className,c=pe(e,["center","classes","className"]),s=t.useState([]),p=s[0],d=s[1],E=t.useRef(0),g=t.useRef(null);t.useEffect(function(){g.current&&(g.current(),g.current=null)},[p]);var v=t.useRef(!1),y=t.useRef(null),h=t.useRef(null),k=t.useRef(null);t.useEffect(function(){return function(){clearTimeout(y.current)}},[]);var x=t.useCallback(function(f){var m=f.pulsate,b=f.rippleX,N=f.rippleY,P=f.rippleSize,F=f.cb;d(function(L){return[].concat(Ue(L),[t.createElement(qe,{key:E.current,classes:a,timeout:ae,pulsate:m,rippleX:b,rippleY:N,rippleSize:P})])}),E.current+=1,g.current=F},[a]),M=t.useCallback(function(){var f=arguments.length>0&&arguments[0]!==void 0?arguments[0]:{},m=arguments.length>1&&arguments[1]!==void 0?arguments[1]:{},b=arguments.length>2?arguments[2]:void 0,N=m.pulsate,P=N===void 0?!1:N,F=m.center,L=F===void 0?n||m.pulsate:F,U=m.fakeElement,Q=U===void 0?!1:U;if(f.type==="mousedown"&&v.current){v.current=!1;return}f.type==="touchstart"&&(v.current=!0);var C=Q?null:k.current,w=C?C.getBoundingClientRect():{width:0,height:0,left:0,top:0},S,V,T;if(L||f.clientX===0&&f.clientY===0||!f.clientX&&!f.touches)S=Math.round(w.width/2),V=Math.round(w.height/2);else{var K=f.touches?f.touches[0]:f,Z=K.clientX,W=K.clientY;S=Math.round(Z-w.left),V=Math.round(W-w.top)}if(L)T=Math.sqrt((2*Math.pow(w.width,2)+Math.pow(w.height,2))/3),T%2===0&&(T+=1);else{var ee=Math.max(Math.abs((C?C.clientWidth:0)-S),S)*2+2,z=Math.max(Math.abs((C?C.clientHeight:0)-V),V)*2+2;T=Math.sqrt(Math.pow(ee,2)+Math.pow(z,2))}f.touches?h.current===null&&(h.current=function(){x({pulsate:P,rippleX:S,rippleY:V,rippleSize:T,cb:b})},y.current=setTimeout(function(){h.current&&(h.current(),h.current=null)},Je)):x({pulsate:P,rippleX:S,rippleY:V,rippleSize:T,cb:b})},[n,x]),$=t.useCallback(function(){M({},{pulsate:!0})},[M]),I=t.useCallback(function(f,m){if(clearTimeout(y.current),f.type==="touchend"&&h.current){f.persist(),h.current(),h.current=null,y.current=setTimeout(function(){I(f,m)});return}h.current=null,d(function(b){return b.length>0?b.slice(1):b}),g.current=m},[]);return t.useImperativeHandle(u,function(){return{pulsate:$,start:M,stop:I}},[$,M,I]),t.createElement("span",q({className:J(a.root,o),ref:k},c),t.createElement(He,{component:null,exit:!0},p))});const et=de(Qe,{flip:!1,name:"MuiTouchRipple"})(t.memo(Ze));var tt={root:{display:"inline-flex",alignItems:"center",justifyContent:"center",position:"relative",WebkitTapHighlightColor:"transparent",backgroundColor:"transparent",outline:0,border:0,margin:0,borderRadius:0,padding:0,cursor:"pointer",userSelect:"none",verticalAlign:"middle","-moz-appearance":"none","-webkit-appearance":"none",textDecoration:"none",color:"inherit","&::-moz-focus-inner":{borderStyle:"none"},"&$disabled":{pointerEvents:"none",cursor:"default"},"@media print":{colorAdjust:"exact"}},disabled:{},focusVisible:{}},nt=t.forwardRef(function(e,u){var l=e.action,n=e.buttonRef,a=e.centerRipple,o=a===void 0?!1:a,c=e.children,s=e.classes,p=e.className,d=e.component,E=d===void 0?"button":d,g=e.disabled,v=g===void 0?!1:g,y=e.disableRipple,h=y===void 0?!1:y,k=e.disableTouchRipple,x=k===void 0?!1:k,M=e.focusRipple,$=M===void 0?!1:M,I=e.focusVisibleClassName,f=e.onBlur,m=e.onClick,b=e.onFocus,N=e.onFocusVisible,P=e.onKeyDown,F=e.onKeyUp,L=e.onMouseDown,U=e.onMouseLeave,Q=e.onMouseUp,C=e.onTouchEnd,w=e.onTouchMove,S=e.onTouchStart,V=e.onDragLeave,T=e.tabIndex,K=T===void 0?0:T,Z=e.TouchRippleProps,W=e.type,ee=W===void 0?"button":W,z=pe(e,["action","buttonRef","centerRipple","children","classes","className","component","disabled","disableRipple","disableTouchRipple","focusRipple","focusVisibleClassName","onBlur","onClick","onFocus","onFocusVisible","onKeyDown","onKeyUp","onMouseDown","onMouseLeave","onMouseUp","onTouchEnd","onTouchMove","onTouchStart","onDragLeave","tabIndex","TouchRippleProps","type"]),O=t.useRef(null);function he(){return Ie.findDOMNode(O.current)}var R=t.useRef(null),se=t.useState(!1),D=se[0],H=se[1];v&&D&&H(!1);var te=ze(),me=te.isFocusVisible,ve=te.onBlurVisible,be=te.ref;t.useImperativeHandle(l,function(){return{focusVisible:function(){H(!0),O.current.focus()}}},[]),t.useEffect(function(){D&&$&&!h&&R.current.pulsate()},[h,$,D]);function _(r,j){var Fe=arguments.length>2&&arguments[2]!==void 0?arguments[2]:x;return A(function(ce){j&&j(ce);var Be=Fe;return!Be&&R.current&&R.current[r](ce),!0})}var ge=_("start",L),Re=_("stop",V),Ee=_("stop",Q),ye=_("stop",function(r){D&&r.preventDefault(),U&&U(r)}),Me=_("start",S),Te=_("stop",C),xe=_("stop",w),Ce=_("stop",function(r){D&&(ve(r),H(!1)),f&&f(r)},!1),we=A(function(r){O.current||(O.current=r.currentTarget),me(r)&&(H(!0),N&&N(r)),b&&b(r)}),ne=function(){var j=he();return E&&E!=="button"&&!(j.tagName==="A"&&j.href)},re=t.useRef(!1),Se=A(function(r){$&&!re.current&&D&&R.current&&r.key===" "&&(re.current=!0,r.persist(),R.current.stop(r,function(){R.current.start(r)})),r.target===r.currentTarget&&ne()&&r.key===" "&&r.preventDefault(),P&&P(r),r.target===r.currentTarget&&ne()&&r.key==="Enter"&&!v&&(r.preventDefault(),m&&m(r))}),Ve=A(function(r){$&&r.key===" "&&R.current&&D&&!r.defaultPrevented&&(re.current=!1,r.persist(),R.current.stop(r,function(){R.current.pulsate(r)})),F&&F(r),m&&r.target===r.currentTarget&&ne()&&r.key===" "&&!r.defaultPrevented&&m(r)}),X=E;X==="button"&&z.href&&(X="a");var Y={};X==="button"?(Y.type=ee,Y.disabled=v):((X!=="a"||!z.href)&&(Y.role="button"),Y["aria-disabled"]=v);var De=ie(n,u),_e=ie(be,O),ke=ie(De,_e),le=t.useState(!1),$e=le[0],Ne=le[1];t.useEffect(function(){Ne(!0)},[]);var Pe=$e&&!h&&!v;return t.createElement(X,q({className:J(s.root,p,D&&[s.focusVisible,I],v&&s.disabled),onBlur:Ce,onClick:m,onFocus:we,onKeyDown:Se,onKeyUp:Ve,onMouseDown:ge,onMouseLeave:ye,onMouseUp:Ee,onDragLeave:Re,onTouchEnd:Te,onTouchMove:xe,onTouchStart:Me,ref:ke,tabIndex:v?-1:K},Y,z),c,Pe?t.createElement(et,q({ref:R,center:o},Z)):null)});const pt=de(tt,{name:"MuiButtonBase"})(nt);export{pt as B};
//# sourceMappingURL=ButtonBase-bbb043b6.js.map
