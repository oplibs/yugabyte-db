import{_ as K}from"./extends-98964cd2.js";import{_ as Q}from"./useTheme-ed20be1d.js";import{r as t}from"./index-f2bd0723.js";import"./index-e297e3bd.js";import{w as U,c as X,a as Y}from"./capitalize-6c71ac81.js";import{F as Z}from"./useFormControl-21824096.js";function s(r,e){return t.isValidElement(r)&&e.indexOf(r.type.muiName)!==-1}function A(r){return r!=null&&!(Array.isArray(r)&&r.length===0)}function j(r){var e=arguments.length>1&&arguments[1]!==void 0?arguments[1]:!1;return r&&(A(r.value)&&r.value!==""||e&&A(r.defaultValue)&&r.defaultValue!=="")}function ee(r){return r.startAdornment}var re={root:{display:"inline-flex",flexDirection:"column",position:"relative",minWidth:0,padding:0,margin:0,border:0,verticalAlign:"top"},marginNormal:{marginTop:16,marginBottom:8},marginDense:{marginTop:8,marginBottom:4},fullWidth:{width:"100%"}},te=t.forwardRef(function(e,N){var i=e.children,o=e.classes,q=e.className,u=e.color,L=u===void 0?"primary":u,f=e.component,R=f===void 0?"div":f,c=e.disabled,m=c===void 0?!1:c,v=e.error,w=v===void 0?!1:v,h=e.fullWidth,p=h===void 0?!1:h,g=e.focused,S=e.hiddenLabel,z=S===void 0?!1:S,_=e.margin,l=_===void 0?"none":_,F=e.required,B=F===void 0?!1:F,V=e.size,$=e.variant,k=$===void 0?"standard":$,D=Q(e,["children","classes","className","color","component","disabled","error","fullWidth","focused","hiddenLabel","margin","required","size","variant"]),C=t.useState(function(){var a=!1;return i&&t.Children.forEach(i,function(n){if(s(n,["Input","Select"])){var W=s(n,["Select"])?n.props.input:n;W&&ee(W.props)&&(a=!0)}}),a}),I=C[0],M=C[1],E=t.useState(function(){var a=!1;return i&&t.Children.forEach(i,function(n){s(n,["Input","Select"])&&j(n.props,!0)&&(a=!0)}),a}),P=E[0],b=E[1],y=t.useState(!1),T=y[0],d=y[1],x=g!==void 0?g:T;m&&x&&d(!1);var O,G=t.useCallback(function(){b(!0)},[]),H=t.useCallback(function(){b(!1)},[]),J={adornedStart:I,setAdornedStart:M,color:L,disabled:m,error:w,filled:P,focused:x,fullWidth:p,hiddenLabel:z,margin:(V==="small"?"dense":void 0)||l,onBlur:function(){d(!1)},onEmpty:H,onFilled:G,onFocus:function(){d(!0)},registerEffect:O,required:B,variant:k};return t.createElement(Z.Provider,{value:J},t.createElement(R,K({className:X(o.root,q,l!=="none"&&o["margin".concat(Y(l))],p&&o.fullWidth),ref:N},D),i))});const se=U(re,{name:"MuiFormControl"})(te);export{se as F,j as a,s as i};
//# sourceMappingURL=FormControl-630baca6.js.map
