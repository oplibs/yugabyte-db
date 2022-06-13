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
  ChangePasswordRequest,
  CreateUserRequest,
  UserAccountListResponse,
  UserResponse,
  UserSpec,
} from '../models';

export interface ChangePasswordForQuery {
  ChangePasswordRequest?: ChangePasswordRequest;
}
export interface CreateUserForQuery {
  CreateUserRequest: CreateUserRequest;
}
export interface ModifyUserForQuery {
  UserSpec?: UserSpec;
}

/**
 * Change user password
 * Change user password
 */


export const changePasswordMutate = (
  body: ChangePasswordForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/public/users/self/password';
  return Axios<unknown>(
    {
      url,
      method: 'PUT',
      data: body.ChangePasswordRequest
    },
    customAxiosInstance
  );
};

export const useChangePasswordMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<unknown, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<unknown, Error, ChangePasswordForQuery, unknown>((props) => {
    return  changePasswordMutate(props, customAxiosInstance);
  }, mutationOptions);
};


/**
 * Create a user
 * Create a user
 */


export const createUserMutate = (
  body: CreateUserForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/public/users';
  return Axios<UserResponse>(
    {
      url,
      method: 'POST',
      data: body.CreateUserRequest
    },
    customAxiosInstance
  );
};

export const useCreateUserMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<UserResponse, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<UserResponse, Error, CreateUserForQuery, unknown>((props) => {
    return  createUserMutate(props, customAxiosInstance);
  }, mutationOptions);
};


/**
 * Delete current user
 * Delete user
 */


export const deleteUserMutate = (
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/public/users/self';
  return Axios<unknown>(
    {
      url,
      method: 'DELETE',
    },
    customAxiosInstance
  );
};

export const useDeleteUserMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<unknown, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<unknown, Error, void, unknown>(() => {
    return  deleteUserMutate(customAxiosInstance);
  }, mutationOptions);
};


/**
 * Get info for current user
 * Get user info
 */

export const getUserAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<UserResponse>(
    {
      url: '/public/users/self',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getUserQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/public/users/self`,
  pageParam,
];


export const useGetUserInfiniteQuery = <T = UserResponse, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<UserResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getUserQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<UserResponse, Error, T>(
    queryKey,
    () => getUserAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetUserQuery = <T = UserResponse, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<UserResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getUserQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<UserResponse, Error, T>(
    queryKey,
    () => getUserAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get a list of accounts associated with the current user
 * Get account information for the user
 */

export const listUserAccountsAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<UserAccountListResponse>(
    {
      url: '/public/users/self/accounts',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const listUserAccountsQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/public/users/self/accounts`,
  pageParam,
];


export const useListUserAccountsInfiniteQuery = <T = UserAccountListResponse, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<UserAccountListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = listUserAccountsQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<UserAccountListResponse, Error, T>(
    queryKey,
    () => listUserAccountsAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useListUserAccountsQuery = <T = UserAccountListResponse, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<UserAccountListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = listUserAccountsQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<UserAccountListResponse, Error, T>(
    queryKey,
    () => listUserAccountsAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Modify user info
 * Modify user info
 */


export const modifyUserMutate = (
  body: ModifyUserForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/public/users/self';
  return Axios<UserResponse>(
    {
      url,
      method: 'PUT',
      data: body.UserSpec
    },
    customAxiosInstance
  );
};

export const useModifyUserMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<UserResponse, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<UserResponse, Error, ModifyUserForQuery, unknown>((props) => {
    return  modifyUserMutate(props, customAxiosInstance);
  }, mutationOptions);
};





