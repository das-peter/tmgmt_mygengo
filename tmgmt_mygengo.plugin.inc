<?php

/**
 * @file
 * Provides myGengo translation plugin controller.
 */

/**
 * myGengo translation plugin controller.
 */
class TMGMTMyGengoTranslatorPluginController extends TMGMTDefaultTranslatorPluginController {

  /**
   * Translation service URL.
   *
   * @var string
   */
  protected $apiURL = 'http://api.mygengo.com/v1.1/';
  protected $sandboxURL = 'http://api.sandbox.mygengo.com/v1.1/';

  /**
   *  requestFromGengo($endpoint, $data = array())
   *
   *  Builds, signs and fires a request to myGengo's API. Will fire to a different url
   *  if on the sandbox.
   *
   *  Authentication rituals are documented here: http://mygengo.com/api/developer-docs/authentication/
   *
   *  It should be noted that the authentication ritual listed there is different from the one here (v1 vs v1.1,
   *  respectively). v1.1 doesn't require signing on the entire data structure, just a timestamp and keys.
   */
  public function requestFromGengo(TMGMTTranslator $translator, $endpoint, $method, $data = array()) {
    $options = array(
      'headers' => array(
        'User-Agent' => 'Drupal myGengo Translation Interface v0.1',
        'Accept' => 'application/json'
      ),
      'method' => $method,
    );

    $timestamp = gmdate('U');
    // Allow to use the internal mocking URL.
    if ($custom_url = $translator->getSetting('url')) {
      $url = $custom_url;
    }
    else {
      $url = $translator->getSetting('use_sandbox') ? $this->sandboxURL : $this->apiURL;
    }
    $url .= $endpoint;

    /**
     *  If it's a GET or DELETE request, just sign it and send it appropriately. PUT/POST
     *  get a little more annoying...
     */
    if ($method == 'GET' || $method == 'DELETE') {
      $query = array_merge(array(
          'api_key' => $translator->getSetting('api_public_key'),
          'api_sig' => hash_hmac('sha1', $timestamp, $translator->getSetting('api_private_key')),
          'ts' => $timestamp
        ), $data);

      $url = url($url, array('query' => $query));
      $response = drupal_http_request($url, $options);
    }
    else {
      $options['headers']['Content-Type'] = 'application/x-www-form-urlencoded';
      $options['data'] = drupal_http_build_query(array(
        'api_key' => $translator->getSetting('api_public_key'),
        'api_sig' => hash_hmac('sha1', $timestamp, $translator->getSetting('api_private_key')),
        'ts' => $timestamp,
        'data' => json_encode($data)
        ));
      $url = url($url);
      $response = drupal_http_request($url, $options);
    }
    $results = json_decode($response->data);
    if ($results->opstat == 'ok' && isset($results->response)) {
      return $results->response;
    }
    else {
      // TODO: Raise an actual error. >_>
      return array();
    }
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::isAvailable().
   */
  public function isAvailable(TMGMTTranslator $translator) {
    if ($translator->getSetting('api_public_key') && $translator->getSetting('api_private_key')) {
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::requestTranslation().
   *
   * Here we will acutally query source and get translations.
   */
  public function requestTranslation(TMGMTJob $job) {
    // Pull the source data array from through the job and generate the MyGengo Translation Job
    $translations = $this->generateMyGengoTranslationJobArray($job, $job->settings['quality']);

    $response = $this->requestFromGengo($job->getTranslator(), 'translate/jobs', 'POST', array(
      'jobs' => $translations,
      'process' => 1,
      'as_group' => 1
      ));

    if (!empty($response)) {
      foreach ($response->jobs as $response_job) {
        // The response is an array of objects, each job is wrapped in another
        // object.
        $response_job = reset($response_job);
        // If tier is "machine", API returns translation immediately.
        if ($response_job->tier == 'machine') {
          $key = array_slice(tmgmt_ensure_keys_array($response_job->custom_data), 1);
          $job->addTranslatedData(array('#text' => $response_job->body_tgt), $key);
        }
      }
      $job->submitted('Job has been submitted.');
    }
    else {
      // @todo: Get a error message from mygengo.
      $job->rejected('Job has been rejected.');
    }
  }

  public function generateMyGengoTranslationJobArray(TMGMTJob $job, $tier = "machine") {
    $data = tmgmt_flatten_data($job->getData());

    $translations = array();

    foreach ($data as $key => $value) {
      if (isset($value['#translate']) && $value['#translate'] === FALSE) {
        continue;
      }

      // NOTE: There's a not-so-fun-bug in the myGengo API that you'll encounter
      // if you change how this key is set. If the key in this hash starts with
      // numbers (especially a 0...), you will get an odd one-off response format
      // that can't be parsed by default.
      //
      // This is being fixed in a future iteration of the API (v2). If you encounter
      // weirdness with this issue, feel free to email us at api@mygengo.com. ;)
      //
      // - Ryan McGrath, who dislikes this error intensely
      $translations['job' . $job->tjid . $key] = array(
        'type' => 'text',
        'slug' => tmgmt_data_item_label($value),
        'body_src' => $value['#text'],
        'lc_src' => $job->source_language,
        'lc_tgt' => $job->target_language,
        'tier' => $tier,
        'callback_url' => url('tmgmt_mygengo_callback', array('absolute' => TRUE)),
        'custom_data' => $job->tjid . '][' . $key,
        'auto_approve' => $job->getSetting('mygengo_auto_approve'),
      );
    }
    return $translations;
  }

  /**
   * Receives and stores a translation returned by myGengo.
   */
  public function receiveTranslation(TMGMTJob $job, $keys, $data) {
    if ($data->status == 'approved') {
      $job->addTranslatedData(array('#text' => $data->body_tgt), $keys);
    }
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::getSupportedTargetLanguages().
   */
  public function getSupportedTargetLanguages(TMGMTTranslator $translator, $source_language) {
    $results = array();
    $targets = $this->requestFromGengo($translator, 'translate/service/language_pairs', 'GET', array('lc_src' => $source_language));
    foreach ($targets as $target) {
      $results[$target->lc_tgt] = $target->lc_tgt;
    }
    return $results;
  }

}
