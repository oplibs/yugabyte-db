import React, { Component, FC, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Layer, Rectangle, Sankey, Tooltip } from 'recharts';
import { ClusterData, useGetClusterNodesQuery } from '@app/api/src';
import { AXIOS_INSTANCE } from '@app/api/src';
import { Box, LinearProgress, Link, makeStyles } from '@material-ui/core';
import { Link as RouterLink } from 'react-router-dom';
import { getInterval, RelativeInterval } from '@app/helpers';
import { getUnixTime } from 'date-fns';
import { StringParam, useQueryParams, withDefault } from 'use-query-params';

const useStyles = makeStyles((theme) => ({
  link: {
    '&:link, &:focus, &:active, &:visited, &:hover': {
      textDecoration: 'none',
      color: theme.palette.text.primary,
    }
  },
}));

interface VCpuUsageSankey {
  cluster: ClusterData,
  height?: number,
  width?: number,
  sankeyProps?: Partial<typeof Sankey['defaultProps'] & { cursor: string }>,
  showTooltip?: boolean,
}

/* Data format for sankey charts

const data = {
  "nodes": [
    { "name": "cores" },
    { "name": "cluster_vpn...1-n5" },
    { "name": "cluster_vpn...1-n6" },
    { "name": "cluster_vpn...1-n7" },
  ],
  "links": [
    { "source": 0, "target": 2, "value": 25 },
    { "source": 0, "target": 1, "value": 12 },
    { "source": 0, "target": 3, "value":  7 },
  ]
};

*/

export const VCpuUsageSankey: FC<VCpuUsageSankey> = ({ cluster, sankeyProps, showTooltip, height, width }) => {
  const { t } = useTranslation();
  const { data: nodesResponse, isFetching } = useGetClusterNodesQuery();

  const [{ nodeName }] = useQueryParams({
    nodeName: withDefault(StringParam, 'all'),
  });
  const filteredNode = nodeName === 'all' || nodeName === '' || !nodeName ? undefined : nodeName; 

  const totalCores = cluster.spec?.cluster_info?.node_info.num_cores ?? 0;

  const [nodeCpuUsage, setNodeCpuUsage] = React.useState<number[]>([]);
  React.useEffect(() => {
    if (!nodesResponse) {
      return;
    }

    const populateCpu = async () => {
      const getNodeCpu = async (nodeName: string) => {
        try {
          const interval = getInterval(RelativeInterval.LastHour);
          // Get the system and user cpu usage of the node from the metrics endpoint
          const cpu = await AXIOS_INSTANCE.get(`/metrics?metrics=CPU_USAGE_SYSTEM%2CCPU_USAGE_USER&node_name=${nodeName}` + 
            `&start_time=${getUnixTime(interval.start)}&end_time=${getUnixTime(interval.end)}`)
            // Add the system and user cpu usage to get the total cpu usage
            .then(({ data }) => {
              const cpuUsageSystemArr = data.data[0].values as any[];
              const cpuUsageSystem = Number((cpuUsageSystemArr.reverse().find(val => val[1] !== undefined) ?? [])[1]) || 0;

              const cpuUsageUserArr = data.data[1].values as any[];
              const cpuUsageUser = Number((cpuUsageUserArr.reverse().find(val => val[1] !== undefined) ?? [])[1]) || 0;

              return (cpuUsageSystem + cpuUsageUser);
            })
            .catch(err => { console.error(err); return 0; })
          return cpu;
        } catch (err) {
          console.error(err);
          return 0;
        }
      }

      const cpuUsage: number[] = [];
      for (let i = 0; i < nodesResponse.data.length; i++) {
        const node = nodesResponse.data[i].name;
        // Fetch the cpu usage of all nodes
        const nodeCPU = await getNodeCpu(node);
        cpuUsage.push(nodeCPU);
      }

      setNodeCpuUsage(cpuUsage);
    }

    populateCpu();
  }, [nodesResponse])

  const data = useMemo(() => {
    const data =  {
      nodes: [
        // Usage node
        { "name": "Usage" },
        // Available node
        { "name": "Available" },
        // Nodes
        ...(nodesResponse?.data.map(({ name, cloud_info: { zone } }) => ({ name, zone })) ?? []),
        // Dummy node for available cores
        { "name": "" }, 
      ],
      links: [ ...(nodesResponse?.data.map((_, index) => ({ 
        // Start all links from the usage node
        "source": 0,
        // Target the corresponding node
        "target": index + 2,
        // Convert node cpu usage to two decimal places
        "value": !nodeCpuUsage[index] || nodeCpuUsage[index] < 1 ? 1 : Math.round(nodeCpuUsage[index] * 100) / 100 }
      )) ?? []),
      // Dummy link for the available cores node
      { "source": 1, "target": (nodesResponse ? nodesResponse.data.length : 0) + 2, value: 0 }
    ],
    }

    // Calculate cpu usage and available cpu values
    const cpuAcc = data["links"].slice(0, -1).reduce((acc, curr) => acc + curr.value, 0);
    const cpuUsage = Math.ceil(Math.min(cpuAcc, 100) * totalCores / 100);
    const cpuAvailable = totalCores - cpuUsage;

    // Update data values as per the calculation performed
    data["links"][data["links"].length - 1].value = Math.round(cpuAcc / cpuUsage * cpuAvailable);
    data["nodes"][0].name = t('clusterDetail.overview.usedCores', { usage: cpuUsage });
    data["nodes"][1].name = t('clusterDetail.overview.availableCores', { available: cpuAvailable });
  
    return data;
  }, [nodeCpuUsage, nodesResponse])

  if (nodeCpuUsage.length === 0 || isFetching) {
    return (
      <Box textAlign="center" pt={9} pb={9} width="100%">
        <LinearProgress />
      </Box>
    );
  }

  return (
    <Sankey
      height={height}
      width={width}
      data={data}
      iterations={0}
      margin={{
        top: 15,
        left: 168,
        right: 225,
        bottom: 5,
      }}
      node={<CpuSankeyNode filteredNode={filteredNode} />}
      nodeWidth={4}
      nodePadding={10}
      link={<CpuSankeyLink nodeWidth={4} filteredNode={filteredNode} />}
      {...sankeyProps}
    >
      {showTooltip && <Tooltip />}
    </Sankey>
  );
};


function CpuSankeyNode(props: any) {
  const classes = useStyles();

  const { x, y, width, height, index, payload, filteredNode } = props;
  const isLeftNode = index <= 1;

  const splitPayload = payload.name.split(' ') as string[];
  const cpuTextPrefix = splitPayload[0].toUpperCase();
  const cpuValue = Number(splitPayload[1])
  const cpuTextSuffix = splitPayload.slice(2).join(' ');

  if (!payload.name) {
    return null;
  }

  return (
    <Layer key={`CustomNode${index}`} opacity={!filteredNode ? 1 : 
      (((isLeftNode && index === 0) || (!isLeftNode && payload.name === filteredNode)) ? 1 : 0.4 )}>
      <Rectangle 
        x={x} y={y} opacity={isLeftNode ? (!filteredNode ? 1 : 0.4) : undefined}
        width={width} height={height} 
        fill={isLeftNode ? "#2B59C3" : "#8047F5"} 
        fillOpacity={isLeftNode ? 0.6 : 0.5} />
      {!isLeftNode ? 
        // Right node
        <Link className={classes.link} component={RouterLink} to={`/performance/metrics?nodeName=${payload.name}`}>
          <text
            textAnchor={'start'}
            x={x + width + 15}
            y={y + height / 2 + width / 2 + 3}
            fontSize="12"
            stroke="#888"
            strokeOpacity="0.5"
          >
            <tspan dx={payload.value < 10 ? 6 : 0}>{payload.value}%</tspan>
            <tspan dx={16}>{payload.name} {payload.zone && `(${payload.zone})`}</tspan>
          </text>
        </Link>
        :
        // Left node
        <text
          textAnchor='end'
          x={x - 10}
          y={y + height / 2 + width / 2 + 3}
          fontSize="13"
          fontWeight={500}
        >
          <tspan fill="#97A5B0">{cpuTextPrefix}</tspan>
          <tspan dx={index === 0 ? (cpuValue < 10 ? 74 : 64) : (cpuValue < 10 ? 40 : 32)} 
            fill="#000" fontWeight={700} fontSize="15">{cpuValue} </tspan>
          <tspan fill="#444" fillOpacity={1}>{cpuTextSuffix}</tspan>
        </text>
      }
    </Layer>
  );
}

class CpuSankeyLink extends Component<any, any> {
  static displayName = 'CpuSankeyLink';

  render() {
    const { sourceX, targetX, sourceY, targetY, sourceControlX, targetControlX, linkWidth, 
      filteredNode, index, nodeWidth, payload } = this.props;

    if (!payload.target.name) {
      return null;
    }

    const gradientID = `linkGradient${index}`;
    const fill = this.state?.fill ?? `url(#${gradientID})`;

    return (
      <Layer key={`CustomLink${index}`} opacity={!filteredNode ? 1 : 
        (payload.target.name === filteredNode ? 1 : 0.4 )}>
        <defs>
          <linearGradient id={gradientID}>
            <stop offset="20%" stopColor={"#2B59C3"} stopOpacity={"0.18"} />
            <stop offset="80%" stopColor={"#8047F5"} stopOpacity={"0.18"} />
          </linearGradient>
        </defs>
        
        <Link component={RouterLink} to={`/performance/metrics?nodeName=${payload.target.name}`}>
          <path
            d={`
              M${sourceX},${sourceY + linkWidth / 2}
              C${sourceControlX},${sourceY + linkWidth / 2}
                ${targetControlX},${targetY + linkWidth / 2}
                ${targetX},${targetY + linkWidth / 2}
              L${targetX},${targetY - linkWidth / 2}
              C${targetControlX},${targetY - linkWidth / 2}
                ${sourceControlX},${sourceY - linkWidth / 2}
                ${sourceX},${sourceY - linkWidth / 2}
              Z
            `}
            fill={fill}
            onMouseEnter={() => {
              this.setState({ fill: 'rgba(0, 136, 254, 0.5)' });
            }}
            onMouseLeave={() => {
              this.setState({ fill: `url(#${gradientID})` });
            }}
          />
        </Link>

        {filteredNode && payload.target.name === filteredNode &&
          <Rectangle 
            x={sourceX - nodeWidth} y={sourceY - linkWidth / 2} 
            width={nodeWidth} height={linkWidth} 
            fill={"#2B59C3"} 
            fillOpacity={0.6} />
        }
      </Layer>
    );
  }
}