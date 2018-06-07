var binding = require('binding').Binding.create('i18n')
var ipc = require('ipc_utils')
var lastError = require('lastError')

binding.registerCustomHook(function (bindingsAPI, extensionId) {
  var apiFunctions = bindingsAPI.apiFunctions
  // var i18n = bindingsAPI.compiledApi

  apiFunctions.setHandleRequest('getAcceptLanguages', function (cb) {
    var responseId = ipc.guid()
    cb && ipc.once('chrome-i18n-getAcceptLanguages-response-' + responseId, function (evt, error, langs) {
      if (error) {
        lastError.run('i18n.getAcceptLanguages', error, '', () => { cb(null) })
      } else {
        cb(langs)
      }
    })

    ipc.send('chrome-i18n-getAcceptLanguages', responseId)
  })
})

exports.$set('binding', binding.generate())
