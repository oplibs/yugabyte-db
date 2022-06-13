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
  MigrationBackupResponse,
  MigrationRestoreResponse,
  MigrationRestoreSpec,
} from '../models';

export interface GetBackupInfoForQuery {
  accountId: string;
  projectId: string;
  backupId: string;
}
export interface RestoreMigrationBackupForQuery {
  accountId: string;
  projectId: string;
  MigrationRestoreSpec?: MigrationRestoreSpec;
}

/**
 * Get a backup
 * Get backup info along with the location
 */

export const getBackupInfoAxiosRequest = (
  requestParameters: GetBackupInfoForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<MigrationBackupResponse>(
    {
      url: '/private/accounts/{accountId}/projects/{projectId}/backups/{backupId}'.replace(`{${'accountId'}}`, encodeURIComponent(String(requestParameters.accountId))).replace(`{${'projectId'}}`, encodeURIComponent(String(requestParameters.projectId))).replace(`{${'backupId'}}`, encodeURIComponent(String(requestParameters.backupId))),
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getBackupInfoQueryKey = (
  requestParametersQuery: GetBackupInfoForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/private/accounts/{accountId}/projects/{projectId}/backups/{backupId}`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetBackupInfoInfiniteQuery = <T = MigrationBackupResponse, Error = ApiError>(
  params: GetBackupInfoForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<MigrationBackupResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getBackupInfoQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<MigrationBackupResponse, Error, T>(
    queryKey,
    () => getBackupInfoAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetBackupInfoQuery = <T = MigrationBackupResponse, Error = ApiError>(
  params: GetBackupInfoForQuery,
  options?: {
    query?: UseQueryOptions<MigrationBackupResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getBackupInfoQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<MigrationBackupResponse, Error, T>(
    queryKey,
    () => getBackupInfoAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Restore a backup to a Cluster
 * Restore a backup from the specified bucket to a Cluster
 */


export const restoreMigrationBackupMutate = (
  body: RestoreMigrationBackupForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/private/accounts/{accountId}/projects/{projectId}/restore_migration'.replace(`{${'accountId'}}`, encodeURIComponent(String(body.accountId))).replace(`{${'projectId'}}`, encodeURIComponent(String(body.projectId)));
  // eslint-disable-next-line
  // @ts-ignore
  delete body.accountId;
  // eslint-disable-next-line
  // @ts-ignore
  delete body.projectId;
  return Axios<MigrationRestoreResponse>(
    {
      url,
      method: 'POST',
      data: body.MigrationRestoreSpec
    },
    customAxiosInstance
  );
};

export const useRestoreMigrationBackupMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<MigrationRestoreResponse, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<MigrationRestoreResponse, Error, RestoreMigrationBackupForQuery, unknown>((props) => {
    return  restoreMigrationBackupMutate(props, customAxiosInstance);
  }, mutationOptions);
};





