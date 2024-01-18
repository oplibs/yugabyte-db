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
  APIForPlanAndAssesPage,
  ApiError,
  IndividualMigrationTaskInfo,
  VoyagerMigrationsInfo,
} from '../models';

export interface GetVoyagerMigrateSchemaTasksForQuery {
  uuid: string;
}
export interface GetVoyagerMigrationAssesmentDetailsForQuery {
  uuid: string;
}
export interface GetVoyagerMigrationTasksForQuery {
  uuid?: string;
  migration_phase?: number;
}

/**
 * Get Voyager data migration metrics
 * Get Voyager data migration metrics
 */

export const getVoyagerMigrateSchemaTasksAxiosRequest = (
  requestParameters: GetVoyagerMigrateSchemaTasksForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<IndividualMigrationTaskInfo>(
    {
      url: '/migrate_schema',
      method: 'GET',
      params: {
        uuid: requestParameters['uuid'],
      }
    },
    customAxiosInstance
  );
};

export const getVoyagerMigrateSchemaTasksQueryKey = (
  requestParametersQuery: GetVoyagerMigrateSchemaTasksForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/migrate_schema`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetVoyagerMigrateSchemaTasksInfiniteQuery = <T = IndividualMigrationTaskInfo, Error = ApiError>(
  params: GetVoyagerMigrateSchemaTasksForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<IndividualMigrationTaskInfo, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getVoyagerMigrateSchemaTasksQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<IndividualMigrationTaskInfo, Error, T>(
    queryKey,
    () => getVoyagerMigrateSchemaTasksAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetVoyagerMigrateSchemaTasksQuery = <T = IndividualMigrationTaskInfo, Error = ApiError>(
  params: GetVoyagerMigrateSchemaTasksForQuery,
  options?: {
    query?: UseQueryOptions<IndividualMigrationTaskInfo, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getVoyagerMigrateSchemaTasksQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<IndividualMigrationTaskInfo, Error, T>(
    queryKey,
    () => getVoyagerMigrateSchemaTasksAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get Voyager data migration metrics
 * Get Voyager data migration metrics
 */

export const getVoyagerMigrationAssesmentDetailsAxiosRequest = (
  requestParameters: GetVoyagerMigrationAssesmentDetailsForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<APIForPlanAndAssesPage>(
    {
      url: '/migration_assesment',
      method: 'GET',
      params: {
        uuid: requestParameters['uuid'],
      }
    },
    customAxiosInstance
  );
};

export const getVoyagerMigrationAssesmentDetailsQueryKey = (
  requestParametersQuery: GetVoyagerMigrationAssesmentDetailsForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/migration_assesment`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetVoyagerMigrationAssesmentDetailsInfiniteQuery = <T = APIForPlanAndAssesPage, Error = ApiError>(
  params: GetVoyagerMigrationAssesmentDetailsForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<APIForPlanAndAssesPage, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getVoyagerMigrationAssesmentDetailsQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<APIForPlanAndAssesPage, Error, T>(
    queryKey,
    () => getVoyagerMigrationAssesmentDetailsAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetVoyagerMigrationAssesmentDetailsQuery = <T = APIForPlanAndAssesPage, Error = ApiError>(
  params: GetVoyagerMigrationAssesmentDetailsForQuery,
  options?: {
    query?: UseQueryOptions<APIForPlanAndAssesPage, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getVoyagerMigrationAssesmentDetailsQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<APIForPlanAndAssesPage, Error, T>(
    queryKey,
    () => getVoyagerMigrationAssesmentDetailsAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get the list of Voyager migrations
 * Get the list of Voyager migrations
 */

export const getVoyagerMigrationTasksAxiosRequest = (
  requestParameters: GetVoyagerMigrationTasksForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<VoyagerMigrationsInfo>(
    {
      url: '/migrations',
      method: 'GET',
      params: {
        uuid: requestParameters['uuid'],
        migration_phase: requestParameters['migration_phase'],
      }
    },
    customAxiosInstance
  );
};

export const getVoyagerMigrationTasksQueryKey = (
  requestParametersQuery: GetVoyagerMigrationTasksForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/migrations`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetVoyagerMigrationTasksInfiniteQuery = <T = VoyagerMigrationsInfo, Error = ApiError>(
  params: GetVoyagerMigrationTasksForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<VoyagerMigrationsInfo, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getVoyagerMigrationTasksQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<VoyagerMigrationsInfo, Error, T>(
    queryKey,
    () => getVoyagerMigrationTasksAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetVoyagerMigrationTasksQuery = <T = VoyagerMigrationsInfo, Error = ApiError>(
  params: GetVoyagerMigrationTasksForQuery,
  options?: {
    query?: UseQueryOptions<VoyagerMigrationsInfo, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getVoyagerMigrationTasksQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<VoyagerMigrationsInfo, Error, T>(
    queryKey,
    () => getVoyagerMigrationTasksAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};






