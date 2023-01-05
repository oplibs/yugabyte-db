// //shown only for aws, gcp, azu, on-pre, k8s
// //disabled for non primary cluster
import React, { ReactElement } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box, Grid } from '@material-ui/core';
import {
  YBLabel,
  YBHelper,
  YBPasswordField,
  YBToggleField,
  YBTooltip
} from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import {
  YSQL_FIELD,
  YSQL_AUTH_FIELD,
  YSQL_PASSWORD_FIELD,
  YSQL_CONFIRM_PASSWORD_FIELD,
  PASSWORD_REGEX,
  YCQL_FIELD
} from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';
import InfoMessage from '../../../../../../assets/info-message.svg';

interface YSQLFieldProps {
  disabled: boolean;
  isAuthEnforced?: boolean;
}

export const YSQLField = ({ disabled, isAuthEnforced }: YSQLFieldProps): ReactElement => {
  const {
    control,
    setValue,
    formState: { errors }
  } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const classes = useFormFieldStyles();
  const YSQLTooltipTitle = t('universeForm.securityConfig.authSettings.enableYSQLHelper');
  const YSQLAuthTooltipTitle = t('universeForm.securityConfig.authSettings.enableYSQLAuthHelper');

  //watchers
  const ysqlEnabled = useWatch({ name: YSQL_FIELD });
  const ycqlEnabled = useWatch({ name: YCQL_FIELD });
  const ysqlAuthEnabled = useWatch({ name: YSQL_AUTH_FIELD });
  const ysqlPassword = useWatch({ name: YSQL_PASSWORD_FIELD });

  //ysqlAuthEnabled cannot be true if ysqlEnabled is false
  useUpdateEffect(() => {
    if (['false', false].includes(ysqlEnabled)) setValue(YSQL_AUTH_FIELD, false);
  }, [ysqlEnabled]);

  return (
    <Box display="flex" width="100%" flexDirection="column" data-testid="YSQLField-Container">
      <Box display="flex">
        <YBTooltip
          title={!ycqlEnabled ? (t('universeForm.instanceConfig.enableYsqlOrYcql') as string) : ''}
          placement="top-start"
        >
          <div>
            <YBToggleField
              name={YSQL_FIELD}
              inputProps={{
                'data-testid': 'YSQLField-EnableToggle'
              }}
              control={control}
              disabled={disabled || !ycqlEnabled}
            />
            {/* <YBHelper dataTestId="YSQLField-EnableHelper">
                {t('universeForm.instanceConfig.enableYSQLHelper')}
              </YBHelper> */}
          </div>
        </YBTooltip>
        <Box flex={1}>
          <YBLabel dataTestId="YSQLField-EnableLabel">
            {t('universeForm.securityConfig.authSettings.enableYSQL')}
            &nbsp;
            <YBTooltip title={YSQLTooltipTitle} className={classes.tooltipText}>
              <img alt="Info" src={InfoMessage} />
            </YBTooltip>
          </YBLabel>
        </Box>
      </Box>

      {ysqlEnabled && (
        <Box mt={3}>
          {!isAuthEnforced && (
            <Box display="flex">
              <YBToggleField
                name={YSQL_AUTH_FIELD}
                inputProps={{
                  'data-testid': 'YSQLField-AuthToggle'
                }}
                control={control}
                disabled={disabled}
              />
              {/* <YBHelper dataTestId="YSQLField-AuthHelper">
                  {t('universeForm.instanceConfig.enableYSQLAuthHelper')}
                </YBHelper> */}
              <Box flex={1}>
                <YBLabel dataTestId="YSQLField-AuthLabel">
                  {t('universeForm.securityConfig.authSettings.enableYSQLAuth')}
                  &nbsp;
                  <YBTooltip title={YSQLAuthTooltipTitle} className={classes.tooltipText}>
                    <img alt="Info" src={InfoMessage} />
                  </YBTooltip>
                </YBLabel>
              </Box>
            </Box>
          )}

          {ysqlAuthEnabled && !disabled && (
            <Box display="flex" flexDirection="column" mt={3}>
              <Grid container spacing={3} direction="column">
                <Grid item sm={12} lg={10}>
                  <Box display="flex">
                    <Box mt={2}>
                      <YBLabel dataTestId="YSQLField-PasswordLabel">
                        {t('universeForm.securityConfig.authSettings.ysqlAuthPassword')}
                      </YBLabel>
                    </Box>
                    <Box flex={1} maxWidth="400px">
                      <YBPasswordField
                        rules={{
                          required:
                            !disabled && ysqlAuthEnabled
                              ? (t('universeForm.validation.required', {
                                  field: t(
                                    'universeForm.securityConfig.authSettings.ysqlAuthPassword'
                                  )
                                }) as string)
                              : '',
                          pattern: {
                            value: PASSWORD_REGEX,
                            message: t('universeForm.validation.passwordStrength')
                          }
                        }}
                        name={YSQL_PASSWORD_FIELD}
                        control={control}
                        fullWidth
                        inputProps={{
                          autoComplete: 'new-password',
                          'data-testid': 'YSQLField-PasswordLabelInput'
                        }}
                        error={!!errors?.instanceConfig?.ysqlPassword}
                        helperText={errors?.instanceConfig?.ysqlPassword?.message}
                      />
                    </Box>
                  </Box>
                </Grid>
                <Grid item sm={12} lg={10}>
                  <Box display="flex">
                    <Box mt={2}>
                      <YBLabel dataTestId="YSQLField-ConfirmPasswordLabel">
                        {t('universeForm.securityConfig.authSettings.confirmPassword')}
                      </YBLabel>
                    </Box>
                    <Box flex={1} maxWidth="400px">
                      <YBPasswordField
                        name={YSQL_CONFIRM_PASSWORD_FIELD}
                        control={control}
                        rules={{
                          validate: {
                            passwordMatch: (value) =>
                              (ysqlAuthEnabled && value === ysqlPassword) ||
                              (t('universeForm.validation.confirmPassword') as string)
                          },
                          deps: [YSQL_PASSWORD_FIELD, YSQL_AUTH_FIELD]
                        }}
                        fullWidth
                        inputProps={{
                          autoComplete: 'new-password',
                          'data-testid': 'YSQLField-ConfirmPasswordInput'
                        }}
                        error={!!errors?.instanceConfig?.ysqlConfirmPassword}
                        helperText={errors?.instanceConfig?.ysqlConfirmPassword?.message}
                      />
                    </Box>
                  </Box>
                </Grid>
              </Grid>
            </Box>
          )}
        </Box>
      )}
    </Box>
  );
};

//shown only for aws, gcp, azu, on-pre, k8s
//disabled for non primary cluster
