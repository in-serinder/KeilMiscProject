const electron = require('electron');
console.log('typeof electron:', typeof electron);
console.log('isString:', typeof electron === 'string');
if (typeof electron === 'string') console.log('path:', electron);
else console.log('keys:', Object.keys(electron).join(','));
