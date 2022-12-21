// tslint:disable
/**
 * Yugabyte Cloud
 * YugabyteDB as a Service
 *
 * The version of the OpenAPI document: v1
 * Contact: support@yugabyte.com
 *
 * NOTE: This class is auto generated by OpenAPI Generator (https://openapi-generator.tech).
 * https://openapi-generator.tech
 * Do not edit the class manually.
 */

// eslint-disable-next-line @typescript-eslint/ban-ts-comment
// @ts-ignore
import { useQuery, useInfiniteQuery, useMutation, UseQueryOptions, UseInfiniteQueryOptions, UseMutationOptions } from 'react-query';
import Axios from '../runtime';
import type { AxiosInstance } from 'axios';
// eslint-disable-next-line @typescript-eslint/ban-ts-comment
// @ts-ignore
import type {
  ApiError,
  ClusterNodesResponse,
  ClusterTableListResponse,
  ClusterTabletListResponse,
  HealthCheckResponse,
  IsLoadBalancerIdle,
  LiveQueryResponseSchema,
  MetricResponse,
  SlowQueryResponseSchema,
  VersionInfo,
} from '../models';

export interface GetClusterMetricForQuery {
  metrics: string;
  node_name?: string;
  region?: string;
  start_time?: number;
  end_time?: number;
}
export interface GetClusterTablesForQuery {
  api?: GetClusterTablesApiEnum;
}
export interface GetLiveQueriesForQuery {
  api?: GetLiveQueriesApiEnum;
}

/**
 * Get health information about the cluster
 * Get health information about the cluster
 */

export const getClusterHealthCheckAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<HealthCheckResponse>(
    {
      url: '/health-check',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getClusterHealthCheckQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/health-check`,
  pageParam,
];


export const useGetClusterHealthCheckInfiniteQuery = <T = HealthCheckResponse, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<HealthCheckResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getClusterHealthCheckQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<HealthCheckResponse, Error, T>(
    queryKey,
    () => getClusterHealthCheckAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetClusterHealthCheckQuery = <T = HealthCheckResponse, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<HealthCheckResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getClusterHealthCheckQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<HealthCheckResponse, Error, T>(
    queryKey,
    () => getClusterHealthCheckAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get metrics for a Yugabyte cluster
 * Get a metric for a cluster
 */

export const getClusterMetricAxiosRequest = (
  requestParameters: GetClusterMetricForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<MetricResponse>(
    {
      url: '/metrics',
      method: 'GET',
      params: {
        metrics: requestParameters['metrics'],
        node_name: requestParameters['node_name'],
        region: requestParameters['region'],
        start_time: requestParameters['start_time'],
        end_time: requestParameters['end_time'],
      }
    },
    customAxiosInstance
  );
};

export const getClusterMetricQueryKey = (
  requestParametersQuery: GetClusterMetricForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/metrics`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetClusterMetricInfiniteQuery = <T = MetricResponse, Error = ApiError>(
  params: GetClusterMetricForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<MetricResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getClusterMetricQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<MetricResponse, Error, T>(
    queryKey,
    () => getClusterMetricAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetClusterMetricQuery = <T = MetricResponse, Error = ApiError>(
  params: GetClusterMetricForQuery,
  options?: {
    query?: UseQueryOptions<MetricResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getClusterMetricQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<MetricResponse, Error, T>(
    queryKey,
    () => getClusterMetricAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get nodes for a Yugabyte cluster
 * Get the nodes for a cluster
 */

export const getClusterNodesAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<ClusterNodesResponse>(
    {
      url: '/nodes',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getClusterNodesQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/nodes`,
  pageParam,
];


export const useGetClusterNodesInfiniteQuery = <T = ClusterNodesResponse, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<ClusterNodesResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getClusterNodesQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<ClusterNodesResponse, Error, T>(
    queryKey,
    () => getClusterNodesAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetClusterNodesQuery = <T = ClusterNodesResponse, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<ClusterNodesResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getClusterNodesQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<ClusterNodesResponse, Error, T>(
    queryKey,
    () => getClusterNodesAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get list of tables per YB API (YCQL/YSQL)
 * Get list of DB tables per YB API (YCQL/YSQL)
 */

export const getClusterTablesAxiosRequest = (
  requestParameters: GetClusterTablesForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<ClusterTableListResponse>(
    {
      url: '/tables',
      method: 'GET',
      params: {
        api: requestParameters['api'],
      }
    },
    customAxiosInstance
  );
};

export const getClusterTablesQueryKey = (
  requestParametersQuery: GetClusterTablesForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/tables`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetClusterTablesInfiniteQuery = <T = ClusterTableListResponse, Error = ApiError>(
  params: GetClusterTablesForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<ClusterTableListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getClusterTablesQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<ClusterTableListResponse, Error, T>(
    queryKey,
    () => getClusterTablesAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetClusterTablesQuery = <T = ClusterTableListResponse, Error = ApiError>(
  params: GetClusterTablesForQuery,
  options?: {
    query?: UseQueryOptions<ClusterTableListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getClusterTablesQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<ClusterTableListResponse, Error, T>(
    queryKey,
    () => getClusterTablesAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get list of tablets
 * Get list of tablets
 */

export const getClusterTabletsAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<ClusterTabletListResponse>(
    {
      url: '/tablets',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getClusterTabletsQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/tablets`,
  pageParam,
];


export const useGetClusterTabletsInfiniteQuery = <T = ClusterTabletListResponse, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<ClusterTabletListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getClusterTabletsQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<ClusterTabletListResponse, Error, T>(
    queryKey,
    () => getClusterTabletsAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetClusterTabletsQuery = <T = ClusterTabletListResponse, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<ClusterTabletListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getClusterTabletsQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<ClusterTabletListResponse, Error, T>(
    queryKey,
    () => getClusterTabletsAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Check if cluster load balancer is idle
 * Check if cluster load balancer is idle
 */

export const getIsLoadBalancerIdleAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<IsLoadBalancerIdle>(
    {
      url: '/is_load_balancer_idle',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getIsLoadBalancerIdleQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/is_load_balancer_idle`,
  pageParam,
];


export const useGetIsLoadBalancerIdleInfiniteQuery = <T = IsLoadBalancerIdle, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<IsLoadBalancerIdle, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getIsLoadBalancerIdleQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<IsLoadBalancerIdle, Error, T>(
    queryKey,
    () => getIsLoadBalancerIdleAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetIsLoadBalancerIdleQuery = <T = IsLoadBalancerIdle, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<IsLoadBalancerIdle, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getIsLoadBalancerIdleQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<IsLoadBalancerIdle, Error, T>(
    queryKey,
    () => getIsLoadBalancerIdleAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get the Live Queries in a Yugabyte Cluster
 * Get the live queries in a cluster
 */

export const getLiveQueriesAxiosRequest = (
  requestParameters: GetLiveQueriesForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<LiveQueryResponseSchema>(
    {
      url: '/live_queries',
      method: 'GET',
      params: {
        api: requestParameters['api'],
      }
    },
    customAxiosInstance
  );
};

export const getLiveQueriesQueryKey = (
  requestParametersQuery: GetLiveQueriesForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/live_queries`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetLiveQueriesInfiniteQuery = <T = LiveQueryResponseSchema, Error = ApiError>(
  params: GetLiveQueriesForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<LiveQueryResponseSchema, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getLiveQueriesQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<LiveQueryResponseSchema, Error, T>(
    queryKey,
    () => getLiveQueriesAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetLiveQueriesQuery = <T = LiveQueryResponseSchema, Error = ApiError>(
  params: GetLiveQueriesForQuery,
  options?: {
    query?: UseQueryOptions<LiveQueryResponseSchema, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getLiveQueriesQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<LiveQueryResponseSchema, Error, T>(
    queryKey,
    () => getLiveQueriesAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get the Slow Queries in a Yugabyte Cluster
 * Get the slow queries in a cluster
 */

export const getSlowQueriesAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<SlowQueryResponseSchema>(
    {
      url: '/slow_queries',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getSlowQueriesQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/slow_queries`,
  pageParam,
];


export const useGetSlowQueriesInfiniteQuery = <T = SlowQueryResponseSchema, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<SlowQueryResponseSchema, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getSlowQueriesQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<SlowQueryResponseSchema, Error, T>(
    queryKey,
    () => getSlowQueriesAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetSlowQueriesQuery = <T = SlowQueryResponseSchema, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<SlowQueryResponseSchema, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getSlowQueriesQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<SlowQueryResponseSchema, Error, T>(
    queryKey,
    () => getSlowQueriesAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get YugabyteDB version
 * Get YugabyteDB version
 */

export const getVersionAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<VersionInfo>(
    {
      url: '/version',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getVersionQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/version`,
  pageParam,
];


export const useGetVersionInfiniteQuery = <T = VersionInfo, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<VersionInfo, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getVersionQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<VersionInfo, Error, T>(
    queryKey,
    () => getVersionAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetVersionQuery = <T = VersionInfo, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<VersionInfo, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getVersionQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<VersionInfo, Error, T>(
    queryKey,
    () => getVersionAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};







/**
  * @export
  * @enum {string}
  */
export enum GetClusterTablesApiEnum {
  Ycql = 'YCQL',
  Ysql = 'YSQL'
}
/**
  * @export
  * @enum {string}
  */
export enum GetLiveQueriesApiEnum {
  Ysql = 'YSQL',
  Ycql = 'YCQL'
}
