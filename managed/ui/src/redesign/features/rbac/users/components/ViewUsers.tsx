/*
 * Created on Mon Jul 31 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useContext, useState } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useToggle } from 'react-use';
import { WithRouterProps } from 'react-router';
import { find } from 'lodash';

import { Box, makeStyles } from '@material-ui/core';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { YBTable } from '../../../../../components/common/YBTable';
import { MoreActionsMenu } from '../../../../../components/customCACerts/MoreActionsMenu';
import { YBButton } from '../../../../components';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { DeleteUserModal } from './DeleteUserModal';
import { YBSearchInput } from '../../../../../components/common/forms/fields/YBSearchInput';

import { RbacValidator, hasNecessaryPerm } from '../../common/RbacValidator';
import { UserPermissionMap } from '../../UserPermPathMapping';
import { getAllUsers, getRoleBindingsForAllUsers } from '../../api';
import { RbacBindings } from './UserUtils';
import { RoleTypeComp } from '../../common/RbacUtils';
import { ybFormatDate } from '../../../../helpers/DateUtils';
import { UserContextMethods, UserPages, UserViewContext } from './UserContext';
import { ForbiddenRoles, Role, RoleType } from '../../roles';
import { RbacUser } from '../interface/Users';
import { Add, ArrowDropDown } from '@material-ui/icons';
import { ReactComponent as User } from '../../../../assets/user.svg';
import { ReactComponent as Delete } from '../../../../assets/trashbin.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(5.5)}px ${theme.spacing(3)}px`,
    '& .yb-table-header th,.yb-table-row td': {
      paddingLeft: '0 !important'
    }
  },
  moreActionsBut: {
    height: '30px',
    padding: '5px 10px',
    borderRadius: theme.spacing(0.75),
    border: '1px solid #C8C8C8',
    background: theme.palette.common.white,
    justifyContent: 'center',
    display: 'flex',
    alignItems: 'center',
    userSelect: 'none',
    width: '90px',
    '& svg': {
      width: theme.spacing(3),
      height: theme.spacing(3)
    }
  },
  roleType: {
    borderRadius: theme.spacing(0.5),
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    padding: '2px 6px',
    '&.custom': {
      border: `1px solid ${theme.palette.primary[300]}`,
      background: theme.palette.primary[200],
      color: theme.palette.primary[600]
    }
  },
  actions: {
    display: 'flex',
    justifyContent: 'space-between',
    marginBottom: theme.spacing(3)
  },
  title: {
    fontSize: '18px',
    fontWeight: 600,
    marginRight: theme.spacing(4)
  },
  search: {
    display: 'flex',
    alignItems: 'center',
    '& .search-input': {
      width: '380px'
    }
  },
  rolesList: {
    overflow: 'unset !important',
    '& > span': {
      marginRight: '6px'
    }
  },
  rolesTd: {
    overflow: 'unset !important',
    display: 'flex'
  },
  moreRoles: {
    color: '#67666C'
  }
}));

export const ViewUsers = ({ routerProps }: { routerProps: WithRouterProps }) => {
  const classes = useStyles();

  const { isLoading, data: users } = useQuery('users', getAllUsers, {
    select: (data) => data.data
  });

  const { isLoading: isRoleBindingsLoading, data: roleBindings } = useQuery(
    'role_bindings',
    getRoleBindingsForAllUsers,
    {
      select: (data) => data.data
    }
  );

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.users.list'
  });

  const [, { setCurrentPage, setCurrentUser }] = (useContext(
    UserViewContext
  ) as unknown) as UserContextMethods;

  const [showDeleteModal, toggleDeleteModal] = useToggle(false);

  const [searchText, setSearchText] = useState('');

  if (isLoading || isRoleBindingsLoading) return <YBLoadingCircleIcon />;

  let filteredUsers = users;

  const viewUserUUID = routerProps.location.query.userUUID;

  if (viewUserUUID) {
    const user = find(users, { uuid: viewUserUUID });
    if (user) {
      setCurrentUser(user);
      setCurrentPage(UserPages.EDIT_USER);
    }
  }

  if (searchText) {
    filteredUsers = filteredUsers?.filter((user: RbacUser) =>
      user.email.toLowerCase().includes(searchText.toLowerCase())
    );
  }

  const getActions = (_: undefined, user: RbacUser) => {
    const userRoles: Role[] = [...(roleBindings?.[user.uuid] ?? [])].map((r) => r.role);
    const isSuperAdmin = userRoles.some((role) =>
      find(ForbiddenRoles, { name: role.name, roleType: role.roleType })
    );

    return (
      <MoreActionsMenu
        menuOptions={[
          {
            text: t('table.moreActions.editAssignedRoles'),
            icon: <User />,
            callback: () => {
              setCurrentUser(user);
              setCurrentPage(UserPages.EDIT_USER);
            },
            disabled:
              !hasNecessaryPerm({ ...UserPermissionMap.updateUser, onResource: user.uuid }) ||
              isSuperAdmin
          },
          {
            text: t('table.moreActions.deleteUser'),
            icon: <Delete />,
            callback: () => {
              if (!hasNecessaryPerm(UserPermissionMap.deleteUser)) return;
              setCurrentUser(user);
              toggleDeleteModal(true);
            },
            disabled:
              !hasNecessaryPerm({ ...UserPermissionMap.deleteUser, onResource: user.uuid }) ||
              user.uuid === localStorage.getItem('userId') ||
              isSuperAdmin
          }
        ]}
      >
        <span className={classes.moreActionsBut}>
          {t('table.actions')} <ArrowDropDown />
        </span>
      </MoreActionsMenu>
    );
  };

  return (
    // <RbacValidator accessRequiredOn={UserPermissionMap.listUser}>
    <Box className={classes.root}>
      <div className={classes.actions}>
        <div className={classes.search}>
          <div className={classes.title} data-testid="users-count">
            {t('rowsCount', { count: users?.length })}
          </div>
          <YBSearchInput
            placeHolder={t('search')}
            onEnterPressed={(val: string) => setSearchText(val)}
          />
        </div>
        <RbacValidator accessRequiredOn={UserPermissionMap.createUser} isControl>
          <YBButton
            startIcon={<Add />}
            size="large"
            variant="primary"
            onClick={() => {
              setCurrentUser(null);
              setCurrentPage(UserPages.CREATE_USER);
            }}
            data-testid={`rbac-resource-create-user`}
          >
            {t('createUser')}
          </YBButton>
        </RbacValidator>
      </div>
      <YBTable data={filteredUsers ?? []}>
        <TableHeaderColumn dataField="uuid" hidden isKey />
        <TableHeaderColumn dataSort dataField="email">
          {t('table.email')}
        </TableHeaderColumn>
        <TableHeaderColumn
          dataSort
          dataField="roles"
          dataFormat={(_role, row: RbacUser) => {
            const roles: RbacBindings[] = [...(roleBindings?.[row.uuid] ?? [])];
            if (roles && roles.length > 0) {
              const minRoles = roles.splice(3);
              return (
                <div className={classes.rolesTd}>
                  <div className={classes.rolesList}>
                    {roles.map((bindings: RbacBindings) => (
                      <RoleTypeComp role={bindings.role} customLabel={bindings.role.name} />
                    ))}
                  </div>
                  {minRoles.length > 0 && (
                    <span className={classes.moreRoles}>
                      {t('table.moreRoles', { count: minRoles.length })}
                    </span>
                  )}
                </div>
              );
            } else {
              return (
                <RoleTypeComp
                  role={{ roleType: RoleType.SYSTEM } as Role}
                  customLabel={t('table.connectOnly')}
                />
              );
            }
          }}
        >
          {t('table.role')}
        </TableHeaderColumn>
        <TableHeaderColumn
          dataSort
          dataField="creationDate"
          dataFormat={(cell) => ybFormatDate(cell)}
        >
          {t('table.createdAt')}
        </TableHeaderColumn>
        <TableHeaderColumn dataField="actions" dataFormat={getActions}>
          {t('table.actions')}
        </TableHeaderColumn>
      </YBTable>
      <DeleteUserModal
        open={showDeleteModal}
        onHide={() => {
          toggleDeleteModal(false);
        }}
      />
    </Box>
    // </RbacValidator>
  );
};
