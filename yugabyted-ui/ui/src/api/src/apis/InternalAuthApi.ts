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
  AdminApiTokenResponse,
  AdminTokenListResponse,
  ApiError,
} from '../models';

export interface DeleteAdminApiTokenForQuery {
  adminTokenId: string;
}
export interface GetAdminApiTokenForQuery {
  userId?: string;
  impersonatingUserEmail?: string;
}
export interface ListAdminApiTokensForQuery {
  user_id?: string;
  impersonating_user_email?: string;
  issuing_authority?: string;
  only_generic_jwts?: boolean;
  order?: string;
  order_by?: string;
  limit?: number;
  continuation_token?: string;
}

/**
 * Delete admin token
 * Delete admin token
 */


export const deleteAdminApiTokenMutate = (
  body: DeleteAdminApiTokenForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/private/auth/admin_token/{adminTokenId}'.replace(`{${'adminTokenId'}}`, encodeURIComponent(String(body.adminTokenId)));
  // eslint-disable-next-line
  // @ts-ignore
  delete body.adminTokenId;
  return Axios<unknown>(
    {
      url,
      method: 'DELETE',
    },
    customAxiosInstance
  );
};

export const useDeleteAdminApiTokenMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<unknown, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<unknown, Error, DeleteAdminApiTokenForQuery, unknown>((props) => {
    return  deleteAdminApiTokenMutate(props, customAxiosInstance);
  }, mutationOptions);
};


/**
 * Create an admin JWT for bearer authentication
 * Create an admin JWT for bearer authentication
 */

export const getAdminApiTokenAxiosRequest = (
  requestParameters: GetAdminApiTokenForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<AdminApiTokenResponse>(
    {
      url: '/private/auth/admin_token',
      method: 'GET',
      params: {
        userId: requestParameters['userId'],
        impersonatingUserEmail: requestParameters['impersonatingUserEmail'],
      }
    },
    customAxiosInstance
  );
};

export const getAdminApiTokenQueryKey = (
  requestParametersQuery: GetAdminApiTokenForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/private/auth/admin_token`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetAdminApiTokenInfiniteQuery = <T = AdminApiTokenResponse, Error = ApiError>(
  params: GetAdminApiTokenForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<AdminApiTokenResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getAdminApiTokenQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<AdminApiTokenResponse, Error, T>(
    queryKey,
    () => getAdminApiTokenAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetAdminApiTokenQuery = <T = AdminApiTokenResponse, Error = ApiError>(
  params: GetAdminApiTokenForQuery,
  options?: {
    query?: UseQueryOptions<AdminApiTokenResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getAdminApiTokenQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<AdminApiTokenResponse, Error, T>(
    queryKey,
    () => getAdminApiTokenAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * List admin JWTs.
 * List admin JWTs
 */

export const listAdminApiTokensAxiosRequest = (
  requestParameters: ListAdminApiTokensForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<AdminTokenListResponse>(
    {
      url: '/private/auth/admin_token/list',
      method: 'GET',
      params: {
        user_id: requestParameters['user_id'],
        impersonating_user_email: requestParameters['impersonating_user_email'],
        issuing_authority: requestParameters['issuing_authority'],
        only_generic_jwts: requestParameters['only_generic_jwts'],
        order: requestParameters['order'],
        order_by: requestParameters['order_by'],
        limit: requestParameters['limit'],
        continuation_token: requestParameters['continuation_token'],
      }
    },
    customAxiosInstance
  );
};

export const listAdminApiTokensQueryKey = (
  requestParametersQuery: ListAdminApiTokensForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/private/auth/admin_token/list`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useListAdminApiTokensInfiniteQuery = <T = AdminTokenListResponse, Error = ApiError>(
  params: ListAdminApiTokensForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<AdminTokenListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = listAdminApiTokensQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<AdminTokenListResponse, Error, T>(
    queryKey,
    () => listAdminApiTokensAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useListAdminApiTokensQuery = <T = AdminTokenListResponse, Error = ApiError>(
  params: ListAdminApiTokensForQuery,
  options?: {
    query?: UseQueryOptions<AdminTokenListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = listAdminApiTokensQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<AdminTokenListResponse, Error, T>(
    queryKey,
    () => listAdminApiTokensAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};






