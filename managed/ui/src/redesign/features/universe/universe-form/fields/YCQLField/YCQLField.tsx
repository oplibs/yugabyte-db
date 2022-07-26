import React, { ReactElement } from 'react';
import { Box, Grid } from '@material-ui/core';
import { useFormContext, useWatch } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { UniverseFormData } from '../../utils/dto';
import { YBLabel, YBHelper, YBPasswordField, YBToggleField } from '../../../../../components';
import {
  YCQL_AUTH_FIELD,
  YCQL_FIELD,
  YCQL_PASSWORD_FIELD,
  YCQL_CONFIRM_PASSWORD_FIELD
} from '../../utils/constants';
interface YCQLFieldProps {
  disabled: boolean;
}

export const YCQLField = ({ disabled }: YCQLFieldProps): ReactElement => {
  const {
    control,
    formState: { errors }
  } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  const ycqlEnabled = useWatch({ name: YCQL_FIELD });
  const ycqlAuthEnabled = useWatch({ name: YCQL_AUTH_FIELD });

  return (
    <Box display="flex" width="100%" flexDirection="column">
      <Box display="flex">
        <YBLabel>{t('universeForm.instanceConfig.enableYCQL')}</YBLabel>
        <Box flex={1}>
          <YBToggleField
            name={YCQL_FIELD}
            inputProps={{
              'data-testid': 'YCQL'
            }}
            control={control}
            disabled={disabled}
          />
          <YBHelper>{t('universeForm.instanceConfig.enableYCQLHelper')}</YBHelper>
        </Box>
      </Box>

      {ycqlEnabled && (
        <Box mt={1}>
          <Box display="flex">
            <YBLabel>{t('universeForm.instanceConfig.enableYCQLAuth')}</YBLabel>
            <Box flex={1}>
              <YBToggleField
                name={YCQL_AUTH_FIELD}
                inputProps={{
                  'data-testid': 'YCQLAuth'
                }}
                control={control}
                disabled={disabled}
              />
              <YBHelper>{t('universeForm.instanceConfig.enableYCQLAuthHelper')}</YBHelper>
            </Box>
          </Box>

          {ycqlAuthEnabled && (
            <Box display="flex">
              <Grid container spacing={3}>
                <Grid item sm={12} lg={6}>
                  <Box display="flex">
                    <YBLabel>{t('universeForm.instanceConfig.YCQLAuthPassword')}</YBLabel>
                    <Box flex={1}>
                      <YBPasswordField
                        name={YCQL_PASSWORD_FIELD}
                        control={control}
                        fullWidth
                        inputProps={{
                          autoComplete: 'new-password',
                          'data-testid': 'InputYcqlPassword'
                        }}
                        error={!!errors?.instanceConfig?.ycqlPassword}
                        helperText={errors?.instanceConfig?.ycqlPassword?.message}
                      />
                    </Box>
                  </Box>
                </Grid>
                <Grid item sm={12} lg={6}>
                  <Box display="flex">
                    <YBLabel>{t('universeForm.instanceConfig.confirmPassword')}</YBLabel>
                    <Box flex={1}>
                      <YBPasswordField
                        name={YCQL_CONFIRM_PASSWORD_FIELD}
                        control={control}
                        fullWidth
                        inputProps={{
                          autoComplete: 'new-password',
                          'data-testid': 'InputConfirmYcqlPassword'
                        }}
                        error={!!errors?.instanceConfig?.ycqlConfirmPassword}
                        helperText={errors?.instanceConfig?.ycqlConfirmPassword?.message}
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
