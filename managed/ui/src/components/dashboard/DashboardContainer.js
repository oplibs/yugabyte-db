// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import Dashboard from './Dashboard';
import { fetchUniverseList, fetchUniverseListSuccess, fetchUniverseListFailure } from '../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseList: () => {
      dispatch(fetchUniverseList())
        .then((response) => {
          if (response.payload.status !== 200) {
            dispatch(fetchUniverseListFailure(response.payload));
          } else {
            dispatch(fetchUniverseListSuccess(response.payload));
          }
        });
    }
  }
}

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universe: state.universe,
    cloud: state.cloud
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(Dashboard);
