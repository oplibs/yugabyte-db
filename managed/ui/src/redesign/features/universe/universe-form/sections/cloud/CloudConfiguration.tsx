import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Typography, Grid } from '@material-ui/core';
import { useSectionStyles } from '../../universeMainStyle';
import {
  AutoPlacementField,
  UniverseNameField,
  PlacementsField,
  ProvidersField,
  RegionsField,
  ReplicationFactor,
  TotalNodesField
} from '../../fields';

interface CloudConfigProps {}

export const CloudConfiguration: FC<CloudConfigProps> = () => {
  const classes = useSectionStyles();
  const { t } = useTranslation();
  return (
    <Box className={classes.sectionContainer}>
      <Typography variant="h5">{t('universeForm.cloudConfig.title')}</Typography>
      <Box width="100%" display="flex" flexDirection="column" justifyContent="center">
        <Box mt={2}>
          <Grid container>
            <Grid lg={6} item container>
              <UniverseNameField disabled={false} />
            </Grid>
          </Grid>
        </Box>
        <Box mt={2}>
          <Grid container>
            <Grid lg={6} item container>
              <ProvidersField disabled={false} />
            </Grid>
          </Grid>
        </Box>
        <Box mt={2}>
          <Grid container>
            <Grid lg={6} item container>
              <RegionsField disabled={false} />
            </Grid>
          </Grid>
        </Box>
        <Box mt={2} mb={4}>
          <Grid container>
            <Grid lg={6} item>
              <ReplicationFactor disabled={false} />
            </Grid>
          </Grid>
        </Box>
        <Typography variant="h5">{t('universeForm.cloudConfig.nodePlacementTitle')}</Typography>
        <Box mt={2} display="flex" flexDirection="column">
          <Grid container justifyContent="space-between" alignItems="center">
            <Grid lg={6} item>
              <AutoPlacementField disabled={false} />
            </Grid>
            <Grid lg={6} item>
              <TotalNodesField disabled={false} />
            </Grid>
          </Grid>
          <PlacementsField disabled={false} />
        </Box>
      </Box>
    </Box>
  );
};
