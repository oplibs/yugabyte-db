// Copyright (c) YugaByte, Inc.

import {
  VALIDATE_FROM_TOKEN, VALIDATE_FROM_TOKEN_SUCCESS, VALIDATE_FROM_TOKEN_FAILURE,
	REGISTER, REGISTER_SUCCESS, REGISTER_FAILURE,
	LOGIN, LOGIN_SUCCESS, LOGIN_FAILURE, LOGOUT, LOGOUT_SUCCESS, LOGOUT_FAILURE,
  FETCH_SOFTWARE_VERSIONS_FAILURE, FETCH_SOFTWARE_VERSIONS_SUCCESS, FETCH_SOFTWARE_VERSIONS
} from '../actions/customers';
import {isValidObject} from '../utils/ObjectUtils';

const INITIAL_STATE = {customer: null, universes: [], tasks: [], status: null,
                       error: null, loading: false, softwareVersions: []};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case VALIDATE_FROM_TOKEN:
      return { ...state, customer: null, status: 'authenticate', error: null, loading: true};
    case VALIDATE_FROM_TOKEN_SUCCESS:
      return { ...state, customer: action.payload, universes: action.payload.universes, status: 'authenticated', error: null, loading: false}; //<-- authenticated
    case VALIDATE_FROM_TOKEN_FAILURE:// return error and make loading = false
      error = "Unable to Authenticate Customer"
      return { ...state, customer: null, status: 'authenticate_failure', error: error, loading: false};
    case REGISTER:// sign up user, set loading = true and status = register
      return { ...state, customer: null, status: 'register', error: null, loading: true};
    case REGISTER_SUCCESS://return user, status = authenticated and make loading = false
      return { ...state, customer: action.payload.data.authToken, status: 'authenticated', error: null, loading: false}; //<-- authenticated
    case REGISTER_FAILURE:// return error and make loading = false
      if (typeof action.payload.data.error === 'string'){
        error = action.payload.data.error;
      } else {
        error = action.payload.data.error.password[0];
      }
      return { ...state, customer: null, status: 'register', error: error, loading: false};
    case LOGIN:// sign in user,  set loading = true and status = login
      return { ...state, customer: null, status: 'login', error: null, loading: true};
    case LOGIN_SUCCESS://return authenticated user,  make loading = false and status = authenticated
      return { ...state, customer: action.payload.data.authToken, status: 'authenticated', error:null, loading: false}; //<-- authenticated
    case LOGIN_FAILURE:// return error and make loading = false
      if (isValidObject(action.payload.data)) {
      if (typeof action.payload.data.error === 'string'){
        error = action.payload.data.error;
      } else {
        error = action.payload.data.error.password[0];
      }
      } else {
        error = "No Response From Server"
      }
      return { ...state, customer: null, status: 'login', error: error, loading: false};
    case LOGOUT:
      return {...state, status: 'logout', error: null, loading: true};
    case LOGOUT_SUCCESS:
      return {...state, customer: null, status: 'logout_success', error: null, loading: false};
    case LOGOUT_FAILURE:
      return {...state, status: 'logout_failure', error: error, loading: false};
    case FETCH_SOFTWARE_VERSIONS:
      return {...state, softwareVersions: []}
    case FETCH_SOFTWARE_VERSIONS_SUCCESS:
      return {...state, softwareVersions: action.payload.data}
    case FETCH_SOFTWARE_VERSIONS_FAILURE:
      return {...state}
    default:
      return state;
  }
}
