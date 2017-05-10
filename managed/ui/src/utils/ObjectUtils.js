// Copyright (c) YugaByte, Inc.

var _ = require('lodash');
var semver = require('semver');

export function isDefinedNotNull(obj) {
  return (typeof obj !== "undefined" && obj !== null);
}
// TODO: Deprecated. Change all references to use isDefinedNotNull instead.
export var isValidObject = isDefinedNotNull;

export function isValidArray(arr) {
  return (isDefinedNotNull(arr) && arr.length && arr.length > 0 && Array.isArray(arr));
}

export function isEmptyObject(obj) {
  return !isProperObject(obj) || Object.keys(obj).length === 0
}

export function isValidFunction(func) {
  return (typeof func === "function");
}

export function isValidNumber(n) {
  return typeof n === 'number' && isFinite(n);
}

// TODO: Rename to isValidObject after changing previous isValidObject references to isDefinedNotNull.
export function isProperObject(obj) {
  return (typeof obj === "object" && obj !== null);
}

export function removeNullProperties(obj) {
  for (var propName in obj) {
    if (obj[propName] === null || obj[propName] === undefined) {
      delete obj[propName];
    }
  }
}

export function trimString(string) {
  return string && string.trim()
}

export function convertSpaceToDash(string) {
  return string && string.replace(/\s+/g, '-');
}

export function sortByLengthOfArrayProperty(array, propertyName) {
  function arrayLengthComparator(item) {
    return item[propertyName] ? item[propertyName].length : 0;
  }
  return _.sortBy(array, arrayLengthComparator);
}

export function groupWithCounts(array) {
  var counts = {};
  array.forEach(function(item) {
    counts[item] = counts[item] || 0;
    counts[item]++;
  });
  return counts;
}

export function sortedGroupCounts(array) {
  var counts = groupWithCounts(array);
  return Object.keys(counts).sort().map(function(item) {
    return {
      value: item,
      count: counts[item],
    };
  });
}

export function areIntentsEqual(userIntent1, userIntent2) {
  return (_.isEqual(userIntent1.numNodes,userIntent2.numNodes)
          && _.isEqual(userIntent1.regionList.sort(), userIntent2.regionList.sort())
          && _.isEqual(userIntent1.deviceInfo, userIntent2.deviceInfo)
          && _.isEqual(userIntent1.replicationFactor, userIntent2.replicationFactor)
          && _.isEqual(userIntent1.provider, userIntent2.provider)
          && _.isEqual(userIntent1.universeName, userIntent2.universeName)
          && _.isEqual(userIntent1.ybSoftwareVersion, userIntent2.ybSoftwareVersion)
          && _.isEqual(userIntent1.accessKeyCode, userIntent2.accessKeyCode))
}

export function areUniverseConfigsEqual(config1, config2) {
  var userIntentsEqual = true;
  var dataObjectsEqual = true;
  if (config1 && config2) {
    if (config1.userIntent && config2.userIntent) {
      userIntentsEqual = areIntentsEqual(config1.userIntent, config2.userIntent);
    }
    dataObjectsEqual = _.isEqual(config1.nodeDetailsSet, config2.nodeDetailsSet) && _.isEqual(config1.placementInfo, config2.placementInfo);
  }
  return dataObjectsEqual && userIntentsEqual;
}

export function normalizeToPositiveInt(value) {
  return parseInt(Math.abs(value), 10) || 0;
}

export function sortVersionStrings(arr) {
  return arr.sort((a,b) => semver.valid(a) && semver.valid(b) ? semver.lt(a,b) : a < b);
}
