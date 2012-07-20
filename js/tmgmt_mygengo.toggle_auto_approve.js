(function ($) {

  Drupal.behaviors.tmgmt_mygengo = {
    attach: function (context, settings) {

      if ($('#edit-settings-use-sandbox').is(':checked')) {
        $('.mygengo-auto-approve input').attr('checked', 1);
      }

      $('.mygengo-use-sandbox input').click(function() {
        if ($('#edit-settings-use-sandbox').is(':checked')) {
          $('.mygengo-auto-approve input').attr('checked', 1);
        }
      });
    }
  };

})(jQuery);
