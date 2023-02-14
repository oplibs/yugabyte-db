/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { ArchitectureType, ProviderCode } from './constants';

// ---------------------------------------------------------------------------
// Each data type is exported as <typeName>Mutation and <typeName>.
//  <typeName> refers to the data type with all its readonly fields included
//  <typeName>Mutation omits readonly fields
// ---------------------------------------------------------------------------

/**
 * Provider type excluding read-only fields
 */
export type YBProviderMutation =
  | AWSProviderMutation
  | AZUProviderMutation
  | GCPProviderMutation
  | K8sProviderMutation
  | OnPremProviderMutation;

/**
 * Provider type excluding write-only fields
 */
export type YBProvider = AWSProvider | AZUProvider | GCPProvider | K8sProvider | OnPremProvider;

export type YBProviderDetailsMutation =
  | AWSProviderDetailsMutation
  | AZUProviderDetailsMutation
  | GCPProviderDetailsMutation
  | K8sProviderDetailsMutation
  | OnPremProviderDetailsMutation;
export type YBProviderDetails =
  | AWSProviderDetails
  | AZUProviderDetails
  | GCPProviderDetails
  | K8sProviderDetails
  | OnPremProviderDetails;

export type YBRegionMutation =
  | AWSRegionMutation
  | AZURegionMutation
  | GCPRegionMutation
  | K8sRegionMutation
  | OnPremRegionMutation;
export type YBRegion = AWSRegion | AZURegion | GCPRegion | K8sRegion | OnPremRegion;

export type YBAvailabilityZoneMutation =
  | AWSAvailabilityZoneMutation
  | AZUAvailabilityZoneMutation
  | GCPAvailabilityZoneMutation
  | K8sAvailabilityZoneMutation
  | OnPremAvailabilityZoneMutation;
export type YBAvailabilityZone =
  | AWSAvailabilityZone
  | AZUAvailabilityZone
  | GCPAvailabilityZone
  | K8sAvailabilityZone
  | OnPremAvailabilityZone;

// Cloud Vendor (AWS, GCP, AZU) Union types
export type CloudVendorAvailabilityZoneMutation =
  | AWSAvailabilityZoneMutation
  | AZUAvailabilityZoneMutation
  | GCPAvailabilityZoneMutation;
export type CloudVendorAvailabilityZone =
  | AWSAvailabilityZone
  | AZUAvailabilityZone
  | GCPAvailabilityZone;
export type CloudVendorRegionMutation = AWSRegionMutation | AZURegionMutation | GCPRegionMutation;

// ---------------------------------------------------------------------------
// Provider Config
// ---------------------------------------------------------------------------
interface ProviderBase {
  code: ProviderCode;
  name: string;
  regions: YBRegion[] | YBRegionMutation[];
  details: YBProviderDetails | YBProviderDetailsMutation;

  config?: { [property: string]: string };
  keyPairName?: string;
  sshPrivateKeyContent?: string;
}
type ProviderMutationBase = ProviderBase;
interface Provider extends ProviderBase {
  active: boolean;
  allAccessKeys: AccessKey[];
  customerUUID: string;
  destVpcId: string;
  hostVpcId: string;
  hostVpcRegion: string;
  keyPairName: string;
  sshPrivateKeyContent: string;
  uuid: string;
  version: number;
}

export interface AWSProviderMutation extends ProviderMutationBase {
  code: typeof ProviderCode.AWS;
  details: AWSProviderDetailsMutation;
  regions: AWSRegionMutation[];
}
export interface AWSProvider extends Provider {
  code: typeof ProviderCode.AWS;
  details: AWSProviderDetails;
  regions: AWSRegion[];
}

export interface AZUProviderMutation extends ProviderMutationBase {
  code: typeof ProviderCode.AZU;
  details: AZUProviderDetailsMutation;
  regions: AZURegionMutation[];
}
export interface AZUProvider extends Provider {
  code: typeof ProviderCode.AZU;
  details: AZUProviderDetails;
  regions: AZURegion[];
}

export interface GCPProviderMutation extends ProviderMutationBase {
  code: typeof ProviderCode.GCP;
  details: GCPProviderDetailsMutation;
  regions: GCPRegionMutation[];
}
export interface GCPProvider extends Provider {
  code: typeof ProviderCode.GCP;
  details: GCPProviderDetails;
  regions: GCPRegion[];
}

export interface K8sProviderMutation extends Omit<ProviderMutationBase, SSHField> {
  code: typeof ProviderCode.KUBERNETES;
  details: K8sProviderDetailsMutation;
  regions: K8sRegionMutation[];
}
export interface K8sProvider extends Omit<Provider, SSHField> {
  code: typeof ProviderCode.KUBERNETES;
  details: K8sProviderDetails;
  regions: K8sRegion[];
}

export interface OnPremProviderMutation extends ProviderMutationBase {
  code: typeof ProviderCode.ON_PREM;
  details: OnPremProviderDetailsMutation;
  regions: OnPremRegionMutation[];
}
export interface OnPremProvider extends Provider {
  code: typeof ProviderCode.ON_PREM;
  details: OnPremProviderDetails;
  regions: OnPremRegion[];
}

// ---------------------------------------------------------------------------
// Access Keys
// ---------------------------------------------------------------------------
export interface AccessKey {
  idKey: {
    keyCode: string;
    providerUUID: string;
  };
  keyInfo: {
    keyPairName: string;
    publicKey: string;
    privateKey: string;
    sshPrivateKeyContent: string;
    vaultPasswordFile: string;
    vaultFile: string;
  };
}

// ---------------------------------------------------------------------------
// Provider Details
// ---------------------------------------------------------------------------
interface ProviderDetailsBase {
  airGapInstall: boolean;
  ntpServers: string[];
  setUpChrony: boolean;
  sshPort: number;
  sshUser: string;

  installNodeExporter?: boolean;
  nodeExporterPort?: number;
  nodeExporterUser?: string;
  passwordlessSudoAccess?: boolean;
  provisionInstanceScript?: string;
  skipProvisioning?: boolean;
}
type ProviderDetailsMutation = ProviderDetailsBase;
interface ProviderDetails extends ProviderDetailsBase {
  showSetUpChrony: boolean; // showSetUpChrony is `True` if the provider was created after PLAT-3009
}

interface AWSCloudInfoBase {
  awsAccessKeyID: string;
  awsAccessKeySecret: string;
  awsHostedZoneId: string;
}
type AWSCloudInfoMutation = AWSCloudInfoBase;
type AWSCloudInfo = AWSCloudInfoBase & {
  awsHostedZoneName: string;
};

interface AZUCloudInfoBase {
  azuClientId: string;
  azuClientSecret: string;
  azuHostedZoneId: string;
  azuRG: string; // azure resource group
  azuSubscriptionId: string;
  azuTenantId: string;
}
type AZUCloudInfoMutation = AZUCloudInfoBase;
type AZUCloudInfo = AZUCloudInfoBase;

interface GCPCloudInfoBase {
  useHostCredentials: boolean;
  useHostVPC: boolean;
  ybFirewallTags: string;

  gceProject?: string;
  customGceNetwork?: string;
}
interface GCPCloudInfoMutation extends GCPCloudInfoBase {
  gceApplicationCredentials?: {};
}
interface GCPCloudInfo extends GCPCloudInfoBase {
  gceApplicationCredentialsPath?: string;
}

interface K8sCloudInfoBase {
  kubeConfigName: string;
  kubernetesImageRegistry: string;
  kubernetesProvider: string;
  kubernetesPullSecretName: string;
  kubernetesServiceAccount: string;

  kubernetesImagePullSecretName?: string;
  kubernetesStorageClasses?: string;
}
interface K8sCloudInfoMutation extends K8sCloudInfoBase {
  kubernetesPullSecretContent: string;

  kubeConfigContent: string; // Kube Config can be specified at the Provider, Region and Zone level
}
interface K8sCloudInfo extends K8sCloudInfoBase {
  kubernetesPullSecret: string; // filepath

  kubeConfig?: string; // filepath
}

interface OnPremCloudInfoBase {
  ybHomeDir: string;
}
type OnPremCloudInfoMutation = OnPremCloudInfoBase;
type OnPremCloudInfo = OnPremCloudInfoBase;

interface AWSProviderDetailsMutation extends ProviderDetailsMutation {
  cloudInfo: { [ProviderCode.AWS]: AWSCloudInfoMutation };
}
interface AWSProviderDetails extends ProviderDetails {
  cloudInfo: { [ProviderCode.AWS]: AWSCloudInfo };
}

interface AZUProviderDetailsMutation extends ProviderDetailsMutation {
  cloudInfo: { [ProviderCode.AZU]: AZUCloudInfoMutation };
}
interface AZUProviderDetails extends ProviderDetails {
  cloudInfo: { [ProviderCode.AZU]: AZUCloudInfo };
}

interface GCPProviderDetailsMutation extends ProviderDetailsMutation {
  cloudInfo: { [ProviderCode.GCP]: GCPCloudInfoMutation };
}
interface GCPProviderDetails extends ProviderDetails {
  cloudInfo: { [ProviderCode.GCP]: GCPCloudInfo };
}

interface K8sProviderDetailsMutation
  extends Omit<ProviderDetailsMutation, NTPServerField | SSHField> {
  cloudInfo: { [ProviderCode.KUBERNETES]: K8sCloudInfoMutation };
}
interface K8sProviderDetails extends Omit<ProviderDetails, NTPServerField | SSHField> {
  cloudInfo: { [ProviderCode.KUBERNETES]: K8sCloudInfo };
}

interface OnPremProviderDetailsMutation extends ProviderDetailsMutation {
  cloudInfo: { [ProviderCode.ON_PREM]: OnPremCloudInfoMutation };
}
interface OnPremProviderDetails extends ProviderDetails {
  cloudInfo: { [ProviderCode.ON_PREM]: OnPremCloudInfo };
}

// ---------------------------------------------------------------------------
// Region
// ---------------------------------------------------------------------------
interface RegionBase {
  code: string; // Cloud provider region code. Ex. us-west-2
  zones: YBAvailabilityZone[] | YBAvailabilityZoneMutation[];

  config?: { [property: string]: string };
}
interface RegionMutation extends RegionBase {
  zones: YBAvailabilityZoneMutation[];
}
interface Region extends RegionBase {
  active: boolean;
  latitude: number;
  longitude: number;
  name: string;
  uuid: string;
  zones: YBAvailabilityZone[];
}

export interface AWSRegionMutation extends RegionMutation {
  details: { cloudInfo: { [ProviderCode.AWS]: AWSRegionCloudInfoMutation } };
  zones: AWSAvailabilityZoneMutation[];
}
export interface AWSRegion extends Region {
  details: { cloudInfo: { [ProviderCode.AWS]: AWSRegionCloudInfo } };
  zones: AWSAvailabilityZone[];
}

export interface AZURegionMutation extends RegionMutation {
  details: { cloudInfo: { [ProviderCode.AZU]: AZURegionCloudInfoMutation } };
  zones: AZUAvailabilityZoneMutation[];
}
export interface AZURegion extends Region {
  details: { cloudInfo: { [ProviderCode.AZU]: AZURegionCloudInfo } };
  zones: AZUAvailabilityZone[];
}

export interface GCPRegionMutation extends RegionMutation {
  details: { cloudInfo: { [ProviderCode.GCP]: GCPRegionCloudInfoMutation } };
  zones: GCPAvailabilityZoneMutation[];
}
export interface GCPRegion extends Region {
  details: { cloudInfo: { [ProviderCode.GCP]: GCPRegionCloudInfo } };
  zones: GCPAvailabilityZone[];
}

export interface K8sRegionMutation extends RegionMutation {
  name: string; // This is required because the `name` field is not derived on the backend before inserting into the db
  details: { cloudInfo: { [ProviderCode.KUBERNETES]: K8sRegionCloudInfoMutation } };
  zones: K8sAvailabilityZoneMutation[];
}
export interface K8sRegion extends Region {
  details: { cloudInfo: { [ProviderCode.KUBERNETES]: K8sRegionCloudInfo } };
  zones: K8sAvailabilityZone[];
}

export interface OnPremRegionMutation extends RegionMutation {
  details: { cloudInfo: Record<string, never> };
  zones: OnPremAvailabilityZoneMutation[];
}
export interface OnPremRegion extends Region {
  details: { cloudInfo: Record<string, never> };
  zones: OnPremAvailabilityZone[];
}

// ---------------------------------------------------------------------------
// Region Cloud Info
// ---------------------------------------------------------------------------

interface AWSRegionCloudInfoBase {
  securityGroupId?: string;
  vnet?: string;

  ybImage?: string;
}
type AWSRegionCloudInfoMutation = AWSRegionCloudInfoBase;
interface AWSRegionCloudInfo extends AWSRegionCloudInfoBase {
  arch: ArchitectureType;
  securityGroupId: string;
  vnet: string;
}

interface AZURegionCloudInfoBase {
  securityGroupId?: string;
  vnet?: string;

  ybImage?: string;
}
type AZURegionCloudInfoMutation = AZURegionCloudInfoBase;
interface AZURegionCloudInfo extends AZURegionCloudInfoBase {
  securityGroupId: string;
  vnet: string;
}

interface GCPRegionCloudInfoBase {
  ybImage?: string;
}
type GCPRegionCloudInfoMutation = GCPRegionCloudInfoBase;
type GCPRegionCloudInfo = GCPRegionCloudInfoBase;

interface K8sRegionCloudInfoBase {
  certManagerClusterIssuer?: string;
  certManagerIssuer?: string;
  kubeDomain?: string;
  kubeNamespace?: string;
  kubePodAddressTemplate?: string;
  overrides?: string;
}
interface K8sRegionCloudInfoMutation
  extends Partial<K8sCloudInfoMutation>,
    K8sRegionCloudInfoBase {}
// TODO: Check whether the following fields are guaranteed from GET API.
interface K8sRegionCloudInfo extends K8sCloudInfo, K8sRegionCloudInfoBase {
  kubeDomain: string;
  kubeNamespace: string;
  kubePodAddressTemplate: string;
  overrides: string;
}

// ---------------------------------------------------------------------------
// Availability Zone
// ---------------------------------------------------------------------------
interface AvailabilityZoneBase {
  code: string;
  subnet: string;

  config?: { [property: string]: string };
  name?: string;
  secondarySubnet?: string;
}

type AvailabilityZoneMutation = AvailabilityZoneBase;
interface AvailabilityZone extends AvailabilityZoneBase {
  active: boolean;
  uuid: string;

  kubeconfigPath?: string;
}

export interface AWSAvailabilityZoneMutation extends AvailabilityZoneMutation {
  name: string;
}
export type AZUAvailabilityZoneMutation = AvailabilityZoneMutation;
export type GCPAvailabilityZoneMutation = AvailabilityZoneMutation;
export interface K8sAvailabilityZoneMutation extends Omit<AvailabilityZoneMutation, 'subnet'> {
  name: string;
  details?: { cloudInfo: { [ProviderCode.KUBERNETES]: K8sAvailabilityZoneCloudInfoMutation } };
}
export type OnPremAvailabilityZoneMutation = AvailabilityZoneMutation;

export type AWSAvailabilityZone = AvailabilityZone;
export type AZUAvailabilityZone = AvailabilityZone;
export type GCPAvailabilityZone = AvailabilityZone;
export interface K8sAvailabilityZone extends AvailabilityZone {
  details?: { cloudInfo: { [ProviderCode.KUBERNETES]: K8sAvailabilityZoneCloudInfo } };
}
export type OnPremAvailabilityZone = AvailabilityZone;

// ---------------------------------------------------------------------------
// Availability Zone Cloud Info
// ---------------------------------------------------------------------------
type K8sAvailabilityZoneCloudInfoMutation = K8sRegionCloudInfoMutation;
type K8sAvailabilityZoneCloudInfo = K8sRegionCloudInfo;

// ---------------------------------------------------------------------------
// Property Groups
// ---------------------------------------------------------------------------
type NTPServerField = 'ntpServers' | 'setUpChrony';
type SSHField = 'sshUser' | 'sshPort' | 'sshPrivateKeyContent' | 'keyPairName';

// ---------------------------------------------------------------------------
// File types
// ---------------------------------------------------------------------------
export interface K8sPullSecretFile {
  apiVerion: string;
  kind: string;
  metadata: {
    name: string;
    namespace?: string;
  };
  data: {
    '.dockerconfigjson': string;
  };
  type: string;
}
