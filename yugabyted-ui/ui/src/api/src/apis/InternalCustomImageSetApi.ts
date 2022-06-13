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
  CreateBulkCustomImageSetResponse,
  CreateBulkCustomImageSetSpecList,
  CustomImageSetListResponse,
  CustomImageSetResponse,
  CustomImageSpec,
} from '../models';

export interface AddCustomImageToSetForQuery {
  customImageSetId: string;
  CustomImageSpec?: CustomImageSpec;
}
export interface CreateCustomImageSetsInBulkForQuery {
  CreateBulkCustomImageSetSpecList?: CreateBulkCustomImageSetSpecList;
}
export interface DeleteCustomImageSetForQuery {
  customImageSetId: string;
}
export interface GetCustomImageSetDetailsForQuery {
  customImageSetId: string;
}
export interface ListCustomImageSetsForQuery {
  cloud_type?: string;
  db_version?: string;
  base_image_name?: string;
  build_reference?: string;
  architecture?: string;
  is_default?: boolean;
  order?: string;
  order_by?: string;
  limit?: number;
  continuation_token?: string;
}
export interface MarkCustomImageSetAsDefaultForQuery {
  customImageSetId: string;
}

/**
 * Add a custom image to the specific custom image set
 * API to add a custom image to the specified custom image set
 */


export const addCustomImageToSetMutate = (
  body: AddCustomImageToSetForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/private/custom_image_sets/{customImageSetId}'.replace(`{${'customImageSetId'}}`, encodeURIComponent(String(body.customImageSetId)));
  // eslint-disable-next-line
  // @ts-ignore
  delete body.customImageSetId;
  return Axios<unknown>(
    {
      url,
      method: 'POST',
      data: body.CustomImageSpec
    },
    customAxiosInstance
  );
};

export const useAddCustomImageToSetMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<unknown, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<unknown, Error, AddCustomImageToSetForQuery, unknown>((props) => {
    return  addCustomImageToSetMutate(props, customAxiosInstance);
  }, mutationOptions);
};


/**
 * API to create custom image sets in bulk
 * API to create custom image sets in bulk
 */


export const createCustomImageSetsInBulkMutate = (
  body: CreateCustomImageSetsInBulkForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/private/custom_image_sets';
  return Axios<CreateBulkCustomImageSetResponse>(
    {
      url,
      method: 'POST',
      data: body.CreateBulkCustomImageSetSpecList
    },
    customAxiosInstance
  );
};

export const useCreateCustomImageSetsInBulkMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<CreateBulkCustomImageSetResponse, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<CreateBulkCustomImageSetResponse, Error, CreateCustomImageSetsInBulkForQuery, unknown>((props) => {
    return  createCustomImageSetsInBulkMutate(props, customAxiosInstance);
  }, mutationOptions);
};


/**
 * Delete custom image set
 * Delete custom image set
 */


export const deleteCustomImageSetMutate = (
  body: DeleteCustomImageSetForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/private/custom_image_sets/{customImageSetId}'.replace(`{${'customImageSetId'}}`, encodeURIComponent(String(body.customImageSetId)));
  // eslint-disable-next-line
  // @ts-ignore
  delete body.customImageSetId;
  return Axios<unknown>(
    {
      url,
      method: 'DELETE',
    },
    customAxiosInstance
  );
};

export const useDeleteCustomImageSetMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<unknown, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<unknown, Error, DeleteCustomImageSetForQuery, unknown>((props) => {
    return  deleteCustomImageSetMutate(props, customAxiosInstance);
  }, mutationOptions);
};


/**
 * Get information about specific custom image set
 * API to get details about custom image set
 */

export const getCustomImageSetDetailsAxiosRequest = (
  requestParameters: GetCustomImageSetDetailsForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<CustomImageSetResponse>(
    {
      url: '/private/custom_image_sets/{customImageSetId}'.replace(`{${'customImageSetId'}}`, encodeURIComponent(String(requestParameters.customImageSetId))),
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getCustomImageSetDetailsQueryKey = (
  requestParametersQuery: GetCustomImageSetDetailsForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/private/custom_image_sets/{customImageSetId}`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetCustomImageSetDetailsInfiniteQuery = <T = CustomImageSetResponse, Error = ApiError>(
  params: GetCustomImageSetDetailsForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<CustomImageSetResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getCustomImageSetDetailsQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<CustomImageSetResponse, Error, T>(
    queryKey,
    () => getCustomImageSetDetailsAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetCustomImageSetDetailsQuery = <T = CustomImageSetResponse, Error = ApiError>(
  params: GetCustomImageSetDetailsForQuery,
  options?: {
    query?: UseQueryOptions<CustomImageSetResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getCustomImageSetDetailsQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<CustomImageSetResponse, Error, T>(
    queryKey,
    () => getCustomImageSetDetailsAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * API to list all custom image sets
 * API to list custom image sets
 */

export const listCustomImageSetsAxiosRequest = (
  requestParameters: ListCustomImageSetsForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<CustomImageSetListResponse>(
    {
      url: '/private/custom_image_sets',
      method: 'GET',
      params: {
        cloud_type: requestParameters['cloud_type'],
        db_version: requestParameters['db_version'],
        base_image_name: requestParameters['base_image_name'],
        build_reference: requestParameters['build_reference'],
        architecture: requestParameters['architecture'],
        is_default: requestParameters['is_default'],
        order: requestParameters['order'],
        order_by: requestParameters['order_by'],
        limit: requestParameters['limit'],
        continuation_token: requestParameters['continuation_token'],
      }
    },
    customAxiosInstance
  );
};

export const listCustomImageSetsQueryKey = (
  requestParametersQuery: ListCustomImageSetsForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/private/custom_image_sets`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useListCustomImageSetsInfiniteQuery = <T = CustomImageSetListResponse, Error = ApiError>(
  params: ListCustomImageSetsForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<CustomImageSetListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = listCustomImageSetsQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<CustomImageSetListResponse, Error, T>(
    queryKey,
    () => listCustomImageSetsAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useListCustomImageSetsQuery = <T = CustomImageSetListResponse, Error = ApiError>(
  params: ListCustomImageSetsForQuery,
  options?: {
    query?: UseQueryOptions<CustomImageSetListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = listCustomImageSetsQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<CustomImageSetListResponse, Error, T>(
    queryKey,
    () => listCustomImageSetsAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Mark a custom image set as default
 * Mark a custom image set as default
 */


export const markCustomImageSetAsDefaultMutate = (
  body: MarkCustomImageSetAsDefaultForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/private/custom_image_sets/{customImageSetId}/default'.replace(`{${'customImageSetId'}}`, encodeURIComponent(String(body.customImageSetId)));
  // eslint-disable-next-line
  // @ts-ignore
  delete body.customImageSetId;
  return Axios<unknown>(
    {
      url,
      method: 'POST',
    },
    customAxiosInstance
  );
};

export const useMarkCustomImageSetAsDefaultMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<unknown, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<unknown, Error, MarkCustomImageSetAsDefaultForQuery, unknown>((props) => {
    return  markCustomImageSetAsDefaultMutate(props, customAxiosInstance);
  }, mutationOptions);
};





