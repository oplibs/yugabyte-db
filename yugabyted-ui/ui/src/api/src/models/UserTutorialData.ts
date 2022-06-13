// tslint:disable
/**
 * Yugabyte Cloud
 * YugabyteDB as a Service
 *
 * The version of the OpenAPI document: v1
 * Contact: support@yugabyte.com
 *
 * NOTE: This class is auto generated by OpenAPI Generator (https://openapi-generator.tech).
 * https://openapi-generator.tech
 * Do not edit the class manually.
 */


// eslint-disable-next-line no-duplicate-imports
import type { UserTutorialInfo } from './UserTutorialInfo';
// eslint-disable-next-line no-duplicate-imports
import type { UserTutorialSpec } from './UserTutorialSpec';


/**
 * User Tutorial Data
 * @export
 * @interface UserTutorialData
 */
export interface UserTutorialData  {
  /**
   * 
   * @type {UserTutorialSpec}
   * @memberof UserTutorialData
   */
  spec: UserTutorialSpec;
  /**
   * 
   * @type {UserTutorialInfo}
   * @memberof UserTutorialData
   */
  info: UserTutorialInfo;
}



