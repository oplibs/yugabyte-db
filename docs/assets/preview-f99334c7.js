import{u as g,n as u,T as s,c as p,j as d}from"./useTheme-ed20be1d.js";import{_ as b}from"./extends-98964cd2.js";import{R as l}from"./index-f2bd0723.js";import"./index-e297e3bd.js";import"./_commonjsHelpers-042e6b4d.js";function h(e,i){if(typeof i=="function"){var t=i(e);return t}return b({},e,i)}function y(e){var i=e.children,t=e.theme,a=g(),c=l.useMemo(function(){var n=a===null?t:h(a,t);return n!=null&&(n[u]=a!==null),n},[t,a]);return l.createElement(s.Provider,{value:c},i)}const o={primary:{100:"#F2F6FF",200:"#E5EDFF",300:"#CBDBFF",400:"#8DABF0",500:"#507CE1",600:"#2B59C3",700:"#1A44A5",800:"#0A2972",900:"#031541"},secondary:{100:"#F1F1F7",200:"#E9E9F2",300:"#CAC9DF",400:"#AAAAD0",500:"#7B7BB8",600:"#4F4FA4",700:"#30307F",800:"#171755",900:"#000041"},grey:{100:"#F0F4F7",200:"#E9EEF2",300:"#D7DEE4",400:"#B7C3CB",500:"#97A5B0",600:"#6D7C88",700:"#4E5F6D",800:"#25323D",900:"#0B1117"},error:{100:"#FDE2E2",300:"#F9ACAC",500:"#DA1515",700:"#8F0000",900:"#590000"},warning:{100:"#FFEEC8",300:"#FFD383",500:"#FFA400",700:"#C88900",900:"#9D6C00"},success:{100:"#CDEFE1",300:"#82D2B0",500:"#13A868",700:"#097345",900:"#024126"},info:{100:"#D7EFF4",200:"#DFF5FF",300:"#9EE7F5",400:"#F8FBFC",500:"#45C8E2",700:"#00819B",900:"#003E4B"},orange:{100:"#F6AB91",300:"#FF6E42",500:"#EF5824",700:"#DC4E1D",900:"#A73D19"},background:{default:"#F7FAFC",paper:"#FFFFFF"},common:{black:"#000000",white:"#FFFFFF",blue:"#36B8F5",magenta:"#D74FEE",purple:"#BB43BC",cyan:"#43BFC2",orange:"#FF6E42",yellow:"#FFFAC8",indigo:"#5E60F0"},chartStroke:{cat1:"#30307F",cat2:"#36B8F5",cat3:"#BB43BC",cat4:"#43BFC2",cat5:"#90948E",cat6:"#1C7180",cat7:"#EEA95F",cat8:"#3590D9",cat9:"#F0679E",cat10:"#707B8E"},chartFill:{area1:"#EAEAF2",area2:"#EBF8FE",area3:"#F8ECF8",area4:"#ECF8F9",area5:"#F4F4F3",area6:"#E8F1F2",area7:"#FDF6EF",area8:"#EBF4FB",area9:"#FDF0F5",area10:"#F0F2F3",bar1:"#0098F0",bar2:"#262666"},ybacolors:{ybOrangeFocus:"#EF582480",ybGray:"#DEDEE0",ybBorderGray:"#E5E5E9",ybDarkGray:"#232329",ybDarkGray1:"#9F9EA7",ybDarkGray2:"#D9D9DB",backgroundGrayLightest:"#FCFCFC",backgroundGrayLight:"#F7F7F7",backgroundGrayDark:"#E5E5E9",inputBackground:"#E6E6E6",backgroundDisabled:"#EEE",colorDisabled:"#555",darkBlue:"#303a78",labelBackground:"#151730"},ybaShadows:{inputBoxShadow:"inset 0 1px 1px rgb(0 0 0 / 8%), 0 0 8px rgb(239 88 36 / 20%)"}},r={screenMinWidth:1024,screenMinHeight:400,sidebarWidthMin:62,sidebarWidthMax:232,footerHeight:40,toolbarHeight:55,inputHeight:42,borderRadius:8,shadowLight:"0 0 4px 0 rgba(0,0,0,0.1)",shadowThick:"0 0 8px 0 rgba(0,0,0,0.1)"},m=p({palette:{primary:{...o.primary,main:o.primary[600]},secondary:{...o.secondary,main:o.secondary[600]},grey:{...o.grey},error:{...o.error,main:o.error[500]},warning:{...o.warning,main:o.warning[500]},success:{...o.success,main:o.success[500]},info:{...o.info,main:o.info[500]},orange:{...o.orange},common:{...o.common},background:{...o.background},text:{primary:o.grey[900],secondary:o.grey[600],disabled:o.grey[600],highlighted:o.common.indigo},action:{hover:o.primary[100],selected:o.primary[200],disabledBackground:o.grey[300]},chart:{stroke:o.chartStroke,fill:o.chartFill},ybacolors:{...o.ybacolors},divider:o.grey[200]},shape:{borderRadius:r.borderRadius,shadowLight:r.shadowLight,shadowThick:r.shadowThick},typography:{fontSize:13,fontFamily:'"Inter", sans-serif',allVariants:{letterSpacing:0,lineHeight:1.25},h1:{fontSize:32,fontWeight:700},h2:{fontSize:24,fontWeight:700},h3:{fontSize:21,fontWeight:700},h4:{fontSize:18,fontWeight:700},h5:{fontSize:15,fontWeight:700},h6:{fontSize:14,fontWeight:500},body1:{fontSize:13,fontWeight:600},body2:{fontSize:13,fontWeight:400},subtitle1:{fontSize:11.5,fontWeight:400},subtitle2:{fontSize:11.5,fontWeight:600},button:{fontSize:11.5,fontWeight:500,textTransform:"uppercase"},caption:{fontSize:10,fontWeight:400,textTransform:"uppercase"}},props:{MuiAppBar:{elevation:0},MuiPaper:{elevation:0},MuiInputLabel:{shrink:!0},MuiInput:{disableUnderline:!0},MuiButton:{disableElevation:!0},MuiButtonBase:{disableRipple:!0}},mixins:{toolbar:{minHeight:r.toolbarHeight,whiteSpace:"nowrap"}},overrides:{MuiAppBar:{root:{border:"none","&.MuiPaper-root":{border:"none",backgroundColor:"transparent"}}},MuiLink:{root:{cursor:"pointer"}},MuiButton:{root:{minWidth:12,height:32},sizeSmall:{height:24},sizeLarge:{height:41,"& $label":{fontSize:15}},label:{fontSize:14,fontWeight:300,textTransform:"none"},startIcon:{marginRight:4,"&$iconSizeLarge":{marginRight:8}},endIcon:{marginLeft:4,"&$iconSizeLarge":{marginLeft:8}},contained:{color:o.common.white,backgroundColor:o.orange[500],"&:hover":{backgroundColor:o.orange[700]},"&:active":{backgroundColor:o.orange[900]},"&$disabled":{backgroundColor:o.grey[300],color:o.grey[600],cursor:"not-allowed",pointerEvents:"unset","&:hover":{backgroundColor:o.grey[300],color:o.grey[600]}}},outlined:{color:o.ybacolors.ybDarkGray,backgroundColor:o.common.white,border:`1px solid ${o.ybacolors.ybGray}`,"&:hover":{backgroundColor:o.ybacolors.ybBorderGray,borderColor:o.ybacolors.ybBorderGray},"&:active":{backgroundColor:o.ybacolors.ybDarkGray2,borderColor:o.ybacolors.ybDarkGray2},"&$disabled":{opacity:.65,color:o.ybacolors.ybDarkGray,cursor:"not-allowed",pointerEvents:"unset","&:hover":{opacity:.65,color:o.ybacolors.ybDarkGray}}},text:{color:o.orange[700],backgroundColor:"transparent","&:hover":{backgroundColor:"transparent"},"&:active":{backgroundColor:"transparent"},"&$disabled":{color:o.grey[600],cursor:"not-allowed",pointerEvents:"unset","&:hover":{color:o.grey[600]}}}},MuiAccordion:{root:{display:"flex",flexDirection:"column",width:"100%","& .MuiIconButton-root":{color:o.primary[900]}}},MuiCheckbox:{root:{"& .MuiSvgIcon-root":{width:16,height:16,color:o.primary[600]}},colorPrimary:{"&.Mui-checked":{color:o.orange[500]}}},MuiAutocomplete:{icon:{color:o.grey[600],right:2},input:{border:0,boxShadow:"none","&:focus":{outline:"none"},marginLeft:3},inputRoot:{minHeight:r.inputHeight,height:"auto !important",padding:4},tag:{backgroundColor:o.ybacolors.inputBackground,borderRadius:6,"&:hover":{backgroundColor:o.ybacolors.inputBackground,opacity:.8},"& .MuiChip-deleteIcon":{width:16,height:16},fontSize:11.5,lineHeight:16,fontWeight:400,height:24,margin:0,marginRight:4,marginBottom:4,border:0},option:{fontSize:13,fontWeight:400,paddingTop:6,paddingBottom:6,minHeight:32,lineHeight:1.25,paddingLeft:"16px !important","&[aria-disabled=true]":{pointerEvents:"auto"}},groupLabel:{fontSize:13,fontWeight:400,paddingTop:6,paddingBottom:6,minHeight:32,lineHeight:1.25,paddingLeft:"16px !important"},"&$disabled":{cursor:"not-allowed"}},MuiInput:{root:{overflow:"hidden",height:r.inputHeight,color:o.grey[900],backgroundColor:o.background.paper,borderRadius:r.borderRadius,border:`1px solid ${o.ybacolors.ybGray}`,"&:hover":{borderColor:o.ybacolors.inputBackground},"&$focused":{borderColor:o.ybacolors.ybOrangeFocus,boxShadow:o.ybaShadows.inputBoxShadow},"&$error":{color:o.error[500],backgroundColor:o.error[100],borderColor:o.error[500],"&:hover":{borderColor:o.error[500]},"&$focused":{boxShadow:`0 0 0 2px ${o.error[100]}`},"&$disabled":{borderColor:o.ybacolors.ybGray}},"&$disabled":{color:o.ybacolors.colorDisabled,backgroundColor:o.ybacolors.backgroundDisabled,borderColor:o.ybacolors.ybGray,cursor:"not-allowed"},"&$multiline":{height:"auto",padding:0}},input:{padding:8,fontWeight:400,fontSize:13,"&::placeholder":{color:o.grey[600]}},formControl:{"label + &":{marginTop:0}}},MuiInputBase:{input:{height:"inherit","&$disabled":{cursor:"not-allowed"}}},MuiChip:{root:{borderRadius:4,fontSize:10}},MuiPopover:{paper:{marginTop:1,fontSize:13,lineHeight:"32px",borderWidth:1,borderColor:o.grey[300],borderStyle:"solid",borderRadius:r.borderRadius,boxShadow:"none"}},MuiInputLabel:{root:{display:"flex",alignItems:"center",color:o.grey[600],fontSize:11.5,lineHeight:"16px",fontWeight:500,textTransform:"uppercase","&$focused":{color:o.grey[600]},"&$error":{color:o.error[500]},"&$disabled":{color:o.grey[600]}},formControl:{position:"relative",marginBottom:4},shrink:{transform:"translate(0, 1.5px)"}},MuiFormHelperText:{root:{fontSize:11.5,lineHeight:"16px",fontWeight:400,textTransform:"none",color:o.grey[600],marginTop:8,"&$error":{"&$disabled":{color:o.grey[600]}}}},MuiSelect:{select:{display:"flex",alignItems:"center","&&":{paddingRight:28},"&:focus":{backgroundColor:"inherit"},"&$disabled":{cursor:"not-allowed"}},icon:{color:o.grey[600],right:2,top:"auto"}},MuiPaper:{root:{border:`1px solid ${o.grey[300]}`}},MuiMenu:{paper:{boxShadow:r.shadowThick}},MuiMenuItem:{root:{height:32,fontSize:13,fontWeight:400,color:o.grey[900]}},MuiListItem:{root:{"&$selected":{backgroundColor:o.primary[200],"&:hover":{backgroundColor:o.primary[200]},"&:focus":{backgroundColor:o.primary[100]}},"&.Mui-focusVisible":{backgroundColor:"unset"}},button:{"&:hover":{backgroundColor:o.primary[100]},"&$selected":{backgroundColor:o.primary[200]},"&:focus":{backgroundColor:o.primary[100]}}},MuiToolbar:{gutters:{"@media (min-width:600px)":{paddingLeft:16,paddingRight:16}}},MuiContainer:{root:{"@media (min-width:600px)":{paddingLeft:16,paddingRight:16}}},MuiFormControl:{root:{justifyContent:"center"}},MuiFormControlLabel:{root:{marginLeft:0,marginBottom:0,"&$disabled":{cursor:"not-allowed",opacity:.6}}},MuiDrawer:{paperAnchorDockedLeft:{border:"none",boxShadow:`inset -1px 0 0 0 ${o.grey[200]}`}},MuiDialog:{paperWidthSm:{width:608},paperWidthMd:{width:800}},MuiDialogContent:{root:{padding:"8px 16px",overflow:"hidden"}},MuiDialogActions:{root:{background:o.grey[200],padding:"16px","& .MuiButtonBase-root":{height:44}}},MuiTooltip:{tooltip:{maxWidth:360,backgroundColor:o.background.paper,color:o.grey[900],border:`1px solid ${o.grey[300]}`,padding:"12px 16px",fontSize:13,fontWeight:400,boxShadow:r.shadowThick},arrow:{"&:before":{color:o.background.paper,border:`1px solid ${o.grey[300]}`}},tooltipPlacementTop:{"@media (min-width: 600px)":{margin:"8px -2px"}}},MuiTabs:{root:{"& .MuiTabs-indicator":{backgroundColor:o.orange[500],height:4},"& .MuiButtonBase-root":{padding:0,textTransform:"none",fontSize:14,fontWeight:500,color:o.ybacolors.labelBackground},borderBottom:`1px solid ${o.ybacolors.ybBorderGray}`}},MuiSnackbar:{anchorOriginBottomRight:{"@media (min-width: 600px)":{bottom:56,right:16}}},MuiPickersBasePicker:{pickerView:{justifyContent:"start",minWidth:280,minHeight:280}},MuiPickersCalendarHeader:{switchHeader:{backgroundColor:o.grey[100],marginTop:0,"& .MuiPickersCalendarHeader-iconButton":{padding:"6px 12px",backgroundColor:o.grey[100]},"& .MuiPickersCalendarHeader-transitionContainer":{height:19}}},MuiPickersCalendar:{week:{"& .MuiTypography-body2":{fontSize:11,fontWeight:500,color:"#333"}}},MuiPickersDay:{day:{fontWeight:300,fontSize:10,borderRadius:5,"&:hover":{backgroundColor:o.primary[200]}},daySelected:{backgroundColor:o.primary[200],borderRadius:5,fontWeight:600,"&:hover":{backgroundColor:o.primary[200]}},dayDisabled:{color:`${o.grey[100]} !important`,"& .MuiTypography-body2":{color:`${o.grey[400]} !important`}},current:{}},MuiPickersModal:{dialogAction:{color:o.primary[400]}},MuiBadge:{badge:{borderRadius:4,fontSize:9,padding:0,height:14,minWidth:16,fontWeight:600},anchorOriginTopRightRectangle:{transform:"scale(1) translate(70%, -50%)"}},MuiTableContainer:{root:{boxShadow:"none",padding:"3px 10px 10px 10px",border:`1px solid ${o.grey[300]}`,"& .MuiPaper-root":{border:0}}},MuiTableHead:{root:{"& .MuiIconButton-root":{padding:"0 4px"},"& .MuiCheckbox-root":{color:o.primary[600]}}},MuiTableBody:{root:{"& .MuiTableRow-root":{"&:last-child td":{borderBottom:0},"& .MuiIconButton-root":{padding:"0 4px"},"& .MuiSwitch-switchBase":{padding:"4px"}}}},MuiTableRow:{root:{"& .actionIcon":{display:"none"},"&:hover .actionIcon":{display:"block"}},hover:{cursor:"pointer"}},MuiTablePagination:{input:{fontWeight:400}},MuiTableCell:{root:{fontWeight:400,lineHeight:1.43,borderBottom:`1px solid ${o.grey[300]}`,"& .PrivateSwitchBase-root-31":{padding:"4px 8px"},"& .MuiTableSortLabel-root":{maxHeight:24}},sizeSmall:{padding:0,lineHeight:"50px"},head:{fontSize:11.5,fontWeight:600,padding:"8px 0",borderBottom:`1px solid ${o.grey[300]}`}},MUIDataTableToolbarSelect:{root:{display:"none"}},MUIDataTableToolbar:{root:{display:"none"}},MuiTableFooter:{root:{"& .MuiTableCell-root":{border:0}}},MUIDataTableBody:{emptyTitle:{padding:"16px 0",fontWeight:400}}}}),M={parameters:{actions:{argTypesRegex:"^on[A-Z].*"},controls:{matchers:{color:/(background|color)$/i,date:/Date$/}}}},B=[e=>d(y,{theme:m,children:d(e,{})})];export{B as decorators,M as default};
//# sourceMappingURL=preview-f99334c7.js.map
