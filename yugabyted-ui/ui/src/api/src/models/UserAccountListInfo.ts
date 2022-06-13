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
import type { AccountData } from './AccountData';


/**
 * 
 * @export
 * @interface UserAccountListInfo
 */
export interface UserAccountListInfo  {
  /**
   * List of admin accounts associated with the user
   * @type {AccountData[]}
   * @memberof UserAccountListInfo
   */
  owned_accounts?: AccountData[];
  /**
   * List of admin accounts associated with the user
   * @type {AccountData[]}
   * @memberof UserAccountListInfo
   */
  admin_accounts?: AccountData[];
  /**
   * List of all accounts associated with the user
   * @type {AccountData[]}
   * @memberof UserAccountListInfo
   */
  linked_accounts?: AccountData[];
  /**
   * List of all accounts associated with the user
   * @type {AccountData[]}
   * @memberof UserAccountListInfo
   */
  accounts?: AccountData[];
}



