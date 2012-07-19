(function ($) {

  Drupal.behaviors.tmgmt_mygengo = {
    attach: function (context, settings) {
      $('.mygengo-use-sandbox input').click(function() {
        if ($('#edit-settings-use-sandbox').is(':checked')) {
          $('.mygengo-auto-approve input').attr('checked', $('.mygengo-use-sandbox input').attr('checked'));
          $('.mygengo-auto-approve input').attr('disabled', 1);
        }
        else {
          $('.mygengo-auto-approve input').attr('disabled', 0);
        }
      });
    }
  };

})(jQuery);
