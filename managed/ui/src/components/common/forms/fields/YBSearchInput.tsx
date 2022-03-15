import { isFunction } from "lodash";
import React from "react";
import { FC } from "react";
import { YBControlledTextInput } from "./YBTextInput";
import './YBSearchInput.scss';

export const YBSearchInput: FC<any> = (props) => {
    return (
      <div className="search-input">
        <i className="fa fa-search" />
        <YBControlledTextInput
          {...props}
          input={{
            onKeyUp: (e: any) =>
              e.key === 'Enter' &&
              isFunction(props.onEnterPressed) &&
              props.onEnterPressed(e.currentTarget.value)
          }}
        />
      </div>
    );
  };