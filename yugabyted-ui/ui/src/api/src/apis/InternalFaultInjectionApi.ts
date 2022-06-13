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
  FaultInjectionListResponse,
  FaultInjectionResponse,
  FaultInjectionSpec,
} from '../models';

export interface ArmFaultInjectionForEntityForQuery {
  FaultInjectionSpec?: FaultInjectionSpec;
}
export interface DisarmFaultInjectionForEntityForQuery {
  FaultInjectionSpec?: FaultInjectionSpec;
}
export interface GetEntityRefsForQuery {
  fault_name: string;
}

/**
 * Arm fault injection
 * Arm fault injection
 */


export const armFaultInjectionForEntityMutate = (
  body: ArmFaultInjectionForEntityForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/private/fault_injection/arm';
  return Axios<unknown>(
    {
      url,
      method: 'POST',
      data: body.FaultInjectionSpec
    },
    customAxiosInstance
  );
};

export const useArmFaultInjectionForEntityMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<unknown, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<unknown, Error, ArmFaultInjectionForEntityForQuery, unknown>((props) => {
    return  armFaultInjectionForEntityMutate(props, customAxiosInstance);
  }, mutationOptions);
};


/**
 * Disarm fault injection
 * Disarm fault injection
 */


export const disarmFaultInjectionForEntityMutate = (
  body: DisarmFaultInjectionForEntityForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/private/fault_injection/disarm';
  return Axios<unknown>(
    {
      url,
      method: 'DELETE',
      data: body.FaultInjectionSpec
    },
    customAxiosInstance
  );
};

export const useDisarmFaultInjectionForEntityMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<unknown, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<unknown, Error, DisarmFaultInjectionForEntityForQuery, unknown>((props) => {
    return  disarmFaultInjectionForEntityMutate(props, customAxiosInstance);
  }, mutationOptions);
};


/**
 * Get armed fault injections
 * Get list of entity refs for the specified fault
 */

export const getEntityRefsAxiosRequest = (
  requestParameters: GetEntityRefsForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<FaultInjectionListResponse>(
    {
      url: '/private/fault_injection/{fault_name}'.replace(`{${'fault_name'}}`, encodeURIComponent(String(requestParameters.fault_name))),
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getEntityRefsQueryKey = (
  requestParametersQuery: GetEntityRefsForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/private/fault_injection/{fault_name}`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetEntityRefsInfiniteQuery = <T = FaultInjectionListResponse, Error = ApiError>(
  params: GetEntityRefsForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<FaultInjectionListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getEntityRefsQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<FaultInjectionListResponse, Error, T>(
    queryKey,
    () => getEntityRefsAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetEntityRefsQuery = <T = FaultInjectionListResponse, Error = ApiError>(
  params: GetEntityRefsForQuery,
  options?: {
    query?: UseQueryOptions<FaultInjectionListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getEntityRefsQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<FaultInjectionListResponse, Error, T>(
    queryKey,
    () => getEntityRefsAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get fault injections
 * Get fault injections
 */

export const getFaultNamesAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<FaultInjectionResponse>(
    {
      url: '/private/fault_injection',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getFaultNamesQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/private/fault_injection`,
  pageParam,
];


export const useGetFaultNamesInfiniteQuery = <T = FaultInjectionResponse, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<FaultInjectionResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getFaultNamesQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<FaultInjectionResponse, Error, T>(
    queryKey,
    () => getFaultNamesAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetFaultNamesQuery = <T = FaultInjectionResponse, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<FaultInjectionResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getFaultNamesQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<FaultInjectionResponse, Error, T>(
    queryKey,
    () => getFaultNamesAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};






