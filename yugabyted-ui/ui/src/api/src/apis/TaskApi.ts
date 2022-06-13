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
  TaskListResponse,
} from '../models';

export interface ListTasksForQuery {
  accountId: string;
  projectId?: string;
  entity_id?: string;
  entity_type?: ListTasksEntityTypeEnum;
  task_type?: ListTasksTaskTypeEnum;
  locking?: boolean;
  order?: string;
  order_by?: string;
  limit?: number;
  continuation_token?: string;
}

/**
 * List tasks
 * List tasks
 */

export const listTasksAxiosRequest = (
  requestParameters: ListTasksForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<TaskListResponse>(
    {
      url: '/public/accounts/{accountId}/tasks'.replace(`{${'accountId'}}`, encodeURIComponent(String(requestParameters.accountId))),
      method: 'GET',
      params: {
        projectId: requestParameters['projectId'],
        entity_id: requestParameters['entity_id'],
        entity_type: requestParameters['entity_type'],
        task_type: requestParameters['task_type'],
        locking: requestParameters['locking'],
        order: requestParameters['order'],
        order_by: requestParameters['order_by'],
        limit: requestParameters['limit'],
        continuation_token: requestParameters['continuation_token'],
      }
    },
    customAxiosInstance
  );
};

export const listTasksQueryKey = (
  requestParametersQuery: ListTasksForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/public/accounts/{accountId}/tasks`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useListTasksInfiniteQuery = <T = TaskListResponse, Error = ApiError>(
  params: ListTasksForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<TaskListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = listTasksQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<TaskListResponse, Error, T>(
    queryKey,
    () => listTasksAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useListTasksQuery = <T = TaskListResponse, Error = ApiError>(
  params: ListTasksForQuery,
  options?: {
    query?: UseQueryOptions<TaskListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = listTasksQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<TaskListResponse, Error, T>(
    queryKey,
    () => listTasksAxiosRequest(params, customAxiosInstance),
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
export enum ListTasksEntityTypeEnum {
  Backup = 'BACKUP',
  Cluster = 'CLUSTER',
  ClusterAllowList = 'CLUSTER_ALLOW_LIST',
  Project = 'PROJECT'
}
/**
  * @export
  * @enum {string}
  */
export enum ListTasksTaskTypeEnum {
  CreateCluster = 'CREATE_CLUSTER',
  EditCluster = 'EDIT_CLUSTER',
  DeleteCluster = 'DELETE_CLUSTER',
  EditAllowList = 'EDIT_ALLOW_LIST',
  CreateBackup = 'CREATE_BACKUP',
  RestoreBackup = 'RESTORE_BACKUP',
  DeleteProject = 'DELETE_PROJECT',
  UpgradeCluster = 'UPGRADE_CLUSTER',
  PauseCluster = 'PAUSE_CLUSTER',
  ResumeCluster = 'RESUME_CLUSTER'
}
