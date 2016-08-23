// Copyright (c) YugaByte, Inc.

import {FETCH_UNIVERSE_INFO, FETCH_UNIVERSE_INFO_SUCCESS, FETCH_UNIVERSE_INFO_FAILURE, RESET_UNIVERSE_INFO} from '../actions/universe';

const INITIAL_STATE = {currentUniverse:null, error:null};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case FETCH_UNIVERSE_INFO:
      return { ...state, loading: true};
    case FETCH_UNIVERSE_INFO_SUCCESS:
      return { ...state, currentUniverse: action.payload.data, error:null, loading: false};
    case FETCH_UNIVERSE_INFO_FAILURE:
      error = action.payload.data || {message: action.payload.error};
      return { ...state, currentUniverse: null, error:error, loading: false};
    case RESET_UNIVERSE_INFO:
      return { ...state, currentUniverse: null, error:null, loading: false};
    default:
      return state;
  }
}
