import UniverseTable from '../components/UniverseTable.js';
import {fetchUniverseList, fetchUniverseListSuccess, fetchUniverseListFailure, resetUniverseList} from '../actions/universe';
import { connect } from 'react-redux';

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
    },
    resetUniverseList: () => {
      dispatch(resetUniverseList());
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe
  };
}

export default connect( mapStateToProps, mapDispatchToProps)(UniverseTable);
