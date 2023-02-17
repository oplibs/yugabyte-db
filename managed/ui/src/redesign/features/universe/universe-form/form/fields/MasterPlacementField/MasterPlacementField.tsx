import React, { ReactElement, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box, Typography, makeStyles } from '@material-ui/core';
import {
  YBRadioGroupField,
  YBLabel,
  YBTooltip,
  RadioGroupOrientation
} from '../../../../../../components';
import { UniverseFormData, MasterPlacementMode, CloudType } from '../../../utils/dto';
import { MASTER_PLACEMENT_FIELD, PROVIDER_FIELD } from '../../../utils/constants';
import InfoMessageIcon from '../../../../../../assets/info-message.svg';

interface MasterPlacementFieldProps {
  isPrimary: boolean;
}

const useStyles = makeStyles((theme) => ({
  tooltipLabel: {
    textDecoration: 'underline',
    marginLeft: theme.spacing(2),
    fontSize: '11.5px',
    fontWeight: 400,
    fontFamily: 'Inter',
    color: '#67666C',
    marginTop: '2px',
    cursor: 'default'
  }
}));

export const MasterPlacementField = ({ isPrimary }: MasterPlacementFieldProps): ReactElement => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const classes = useStyles();
  const { t } = useTranslation();

  // Tooltip message
  const masterPlacementTooltipText = t('universeForm.cloudConfig.masterPlacementHelper');

  // watcher
  const masterPlacement = useWatch({ name: MASTER_PLACEMENT_FIELD });
  const provider = useWatch({ name: PROVIDER_FIELD });

  useEffect(() => {
    if (!isPrimary) {
      setValue(MASTER_PLACEMENT_FIELD, MasterPlacementMode.COLOCATED);
    }
  }, [isPrimary]);

  return (
    <>
      {isPrimary && provider?.code !== CloudType.kubernetes ? (
        <Box display="flex" width="100%" data-testid="MasterPlacement-Container">
          <Box>
            <YBLabel dataTestId="MasterPlacement-Label">
              {t('universeForm.cloudConfig.masterPlacement')}
              &nbsp;
              <img alt="More" src={InfoMessageIcon} />
            </YBLabel>
          </Box>
          <Box flex={1}>
            <YBRadioGroupField
              name={MASTER_PLACEMENT_FIELD}
              control={control}
              value={masterPlacement}
              orientation={RadioGroupOrientation.VERTICAL}
              onChange={(e) => {
                setValue(MASTER_PLACEMENT_FIELD, e.target.value as MasterPlacementMode);
              }}
              options={[
                {
                  value: MasterPlacementMode.COLOCATED,
                  label: (
                    <Box display="flex">{t('universeForm.cloudConfig.colocatedModeHelper')}</Box>
                  )
                },
                {
                  value: MasterPlacementMode.DEDICATED,
                  label: (
                    <Box display="flex">
                      {t('universeForm.cloudConfig.dedicatedModeHelper')}
                      <YBTooltip
                        title={masterPlacementTooltipText}
                        className={classes.tooltipLabel}
                      >
                        <Typography display="inline">
                          {t('universeForm.cloudConfig.whenToUseDedicatedHelper')}
                        </Typography>
                      </YBTooltip>
                    </Box>
                  )
                }
              ]}
            />
          </Box>
        </Box>
      ) : (
        <Box mb={2}></Box>
      )}
    </>
  );
};
