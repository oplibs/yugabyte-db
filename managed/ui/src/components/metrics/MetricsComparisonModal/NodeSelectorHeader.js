import React, { useContext } from 'react';
import './MetricsComparisonModal.scss';
import { NodeSelector } from './NodeSelector';
import { FilterContext } from './ComparisonFilterContextProvider';

export const NodeSelectorHeader = ({ universe }) => {
  const [state, dispatch] = useContext(FilterContext);

  const handleNodeChange = (type, event) => {
    const nodeName = event.target.value;
    dispatch({
      type: type,
      payload: nodeName
    });
  };

  return (
    <div className="node-selector-header">
      <NodeSelector
        selectedUniverse={universe}
        selectedNode={state.nodeNameFirst}
        otherSelectedNode={state.nodeNameSecond}
        nodeItemChanged={(nodeName) => handleNodeChange('CHANGE_FIRST_NODE', nodeName)}
      />
      <div className="node-compare">
        <i className="node-compare-icon fa fa-compress" />
      </div>
      <NodeSelector
        selectedUniverse={universe}
        selectedNode={state.nodeNameSecond}
        otherSelectedNode={state.nodeNameFirst}
        nodeItemChanged={(nodeName) => handleNodeChange('CHANGE_SECOND_NODE', nodeName)}
      />
    </div>
  );
};
