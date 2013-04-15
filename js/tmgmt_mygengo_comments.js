(function ($) {

  Drupal.behaviors.tmgmt_mygengo = {
    attach: function (context, settings) {

      $('input.new-comment-button').click(function() {
        var classes = $(this).attr('class').split(' ');
        $(classes).each(function() {
          if (this.indexOf('gengo-id') > 0) {
            var gengoId = this.replace('-gengo-id', '');
            $("#" + gengoId + "-input-wrapper").slideDown();
            $("#" + gengoId + "-input-wrapper textarea").focus();
          }
        });
        return false;
      });

      $('input.cancel-comment-button').click(function() {
        var classes = $(this).attr('class').split(' ');
        $(classes).each(function() {
          if (this.indexOf('gengo-id') > 0) {
            var gengoId = this.replace('-gengo-id', '');
            $("#" + gengoId + "-input-wrapper").slideUp();
          }
        });
        return false;
      });

      var gengoId = $('input:hidden[name=submitted_comment_gengo_id]').val();
      if (gengoId) {
        $('html, body').animate({
          scrollTop: $("#" + gengoId + "-comments-list-closing").offset().top - 200
        }, 200);
        $("#" + gengoId + "-input-wrapper").hide();
      }
    }
  };

})(jQuery);
