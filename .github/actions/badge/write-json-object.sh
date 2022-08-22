#!/bin/bash
for var in "$@"
do
    echo "$var"
done

#mkdir -p badges/${{ inputs.category }}/${{ inputs.label }}
#echo '{ "schemaVersion": 1, "label": "${{ inputs.label }}", "message": "${{ inputs.message }}", "color": "${{ inputs.color }}" }' > badges/${{ inputs.category }}/${{ inputs.label }}/shields.json

#- if: job.status == 'success'
#      uses: ./.github/actions/badge/write
#      with:
#        category: ${{ inputs.category }}
#        label: ${{ inputs.label }}
#    - if: job.status == 'failure'
#      uses: ./.github/actions/badge/write
#      with:
#        category: ${{ inputs.category }}
#        label: ${{ inputs.label }}
#        message: Failing
#        color: red